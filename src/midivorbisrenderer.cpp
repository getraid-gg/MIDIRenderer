#include "midivorbisrenderer.h"

#include <exception>
#include <fstream>
#include <random>
#include <ctime>

#include <fluidsynth.h>

#include "platformsupport.h"
#include "oggvorbisencoder.h"
#include "songrendercontainer.h"

namespace midirenderer
{
	struct PlayerCallbackData
	{
		int m_loopTick;
		int m_queuedSeek;
		bool m_hasHitLoopPoint;

		PlayerCallbackData() : m_loopTick(-1), m_queuedSeek(-1), m_hasHitLoopPoint(false) { }
	};

	MIDIVorbisRenderer::MIDIVorbisRenderer(LoopMode loopMode, int endingBeatDivision) :
		m_loopMode(loopMode), m_endingBeatDivision(endingBeatDivision),
		m_fluidSettings(nullptr, nullptr),
		m_synth(nullptr, nullptr)
	{
		m_fluidSettings = deleter_unique_ptr<fluid_settings_t>(new_fluid_settings(), delete_fluid_settings);

		fluid_settings_setnum(m_fluidSettings.get(), "synth.sample-rate", 44100.0);
		fluid_settings_setint(m_fluidSettings.get(), "synth.chorus.active", 0);
		fluid_settings_setint(m_fluidSettings.get(), "synth.reverb.active", 0);
		fluid_settings_setnum(m_fluidSettings.get(), "synth.gain", 0.5);
		fluid_settings_setstr(m_fluidSettings.get(), "player.timing-source", "sample");
		// Don't reset just in case stopping and starting resets it - we want playback to be seamless
		fluid_settings_setint(m_fluidSettings.get(), "player.reset-synth", 0);
		// From the docs: "since this is a non-realtime scenario, there is no need to pin the sample data"
		fluid_settings_setint(m_fluidSettings.get(), "synth.lock-memory", 0);

		m_synth = deleter_unique_ptr<fluid_synth_t>(new_fluid_synth(m_fluidSettings.get()), delete_fluid_synth);
	}

	void MIDIVorbisRenderer::loadSoundfont(std::string soundfontPath)
	{
		if (getHasSoundfont())
		{
			fluid_synth_sfunload(m_synth.get(), fluid_sfont_get_id(fluid_synth_get_sfont(m_synth.get(), 0)), true);
		}

		int soundfontLoadResult = fluid_synth_sfload(m_synth.get(), soundfontPath.c_str(), 1);
		if (soundfontLoadResult == FLUID_FAILED)
		{
			throw std::invalid_argument("Failed to load the soundfont at " + soundfontPath);
		}
	}

	void MIDIVorbisRenderer::renderFile(std::string sourcePath, std::string outputPath)
	{
		if (!getHasSoundfont())
		{
			throw std::runtime_error("Cannot render with no soundfont loaded");
		}

		std::default_random_engine rng;
		rng.seed(time(NULL));
		OggVorbisEncoder encoder = OggVorbisEncoder(static_cast<int>(rng()), 44100, 0.4);

		//PlayerCallbackData callbackData(nullptr, nullptr);
		PlayerCallbackData callbackData;
		uint64_t samplePosition = 0;
		bool hasLoopPoint = false;
		uint64_t loopPoint = 0;

		//renderSong(callbackData, sourcePath, encoder, hasLoopPoint, loopPoint, samplePosition);
		renderSong(callbackData, sourcePath, encoder, hasLoopPoint, loopPoint, samplePosition);

		encoder.addComment("ENCODER", "libvorbis (midirenderer)");
		if (hasLoopPoint)
		{
			encoder.addComment("LOOPSTART", std::to_string(loopPoint));
			encoder.addComment("LOOPLENGTH", std::to_string(samplePosition - loopPoint));
		}

		std::ofstream fileOutput;
		fileOutput.open(stringutils::getPlatformString(outputPath), std::ios_base::out | std::ios_base::binary);
		auto pageCallback = [&](const unsigned char* header, long headerLength, const unsigned char* body, long bodyLength)
		{
			fileOutput.write(reinterpret_cast<const char*>(header), headerLength);
			fileOutput.write(reinterpret_cast<const char*>(body), bodyLength);
		};

		encoder.readHeader(pageCallback);
		encoder.completeStream(pageCallback);

		return;
	}

	bool MIDIVorbisRenderer::getHasSoundfont()
	{
		return fluid_synth_sfcount(m_synth.get()) > 0;
	}

	void MIDIVorbisRenderer::renderSong(PlayerCallbackData& callbackData, std::string fileName, OggVorbisEncoder& encoder, bool& hasLoopPoint, uint64_t& loopPoint, uint64_t& samplePosition)
	{
		SongRenderContainer songRenderer = SongRenderContainer(fileName, fluid_synth_get_sfont(m_synth.get(), 0));

		songRenderer.setMIDICallback(playerEventCallback, &callbackData);
		songRenderer.startPlayback();

		float leftBuffer[s_audioBufferSize];
		float rightBuffer[s_audioBufferSize];
		size_t bufferIndex = 0;

		int lastTempo = songRenderer.getTempo();
		uint64_t lastTempoSample = samplePosition;
		uint64_t loopStartSample = 0;

		if (!songRenderer.getIsPlaying())
		{
			throw std::runtime_error("Failed to play MIDI file " + fileName);
		}

		while (songRenderer.getIsPlaying())
		{
			readSampleFromSynth(songRenderer, leftBuffer, rightBuffer, bufferIndex, encoder);

			if (!hasLoopPoint && callbackData.m_hasHitLoopPoint)
			{
				hasLoopPoint = true;
				loopPoint = samplePosition;
				// The loop point actually happened one buffer ago so we need to move the loop point backward
				loopPoint -= songRenderer.getSynthBufferSize();
				loopStartSample = samplePosition;
			}

			int tempo = songRenderer.getTempo();
			if (tempo != lastTempo)
			{
				lastTempo = tempo;
				lastTempoSample = samplePosition;
			}
			samplePosition++;
		}

		songRenderer.join();

		renderToBeatDivision(songRenderer, samplePosition, lastTempoSample, lastTempo, leftBuffer, rightBuffer, bufferIndex, encoder);

		// To ensure no non-runoff samples are written to the encoder as overlap samples,
		// all buffered samples need to be written to the encoder before playing voice runoff
		flushBuffersToEncoder(leftBuffer, rightBuffer, bufferIndex, encoder);
		songRenderer.silence();

		// Play the voice runoff of the end, which may or may not end up part of the loop

		// samplePosition isn't incremented here because it's used to determine loop points
		// and the runoff is not meant to delay the loop point at the end of the song.
		encoder.startOverlapRegion();

		size_t overlapSamples = 0;
		while (songRenderer.getActiveVoiceCount() > 0)
		{
			readSampleFromSynth(songRenderer, leftBuffer, rightBuffer, bufferIndex, encoder);
			overlapSamples++;
		}
		flushBuffersToEncoder(leftBuffer, rightBuffer, bufferIndex, encoder);

		encoder.endOverlapRegion();

		// When looping in-file, the runoff period is used to transition to a partial second
		// playthrough of the song, which is the same length of the runoff period. In theory,
		// this means that the loop is made seamless since the sound from the end of the loop
		// carry into the sound at the beginning of the loop.
		if (m_loopMode != LoopMode::None)
		{
			songRenderer.resetPlayer();

			switch (m_loopMode)
			{
			case LoopMode::Short:
			{
				renderShortLoop(songRenderer, leftBuffer, rightBuffer, bufferIndex, encoder,
					loopStartSample, overlapSamples, samplePosition, loopPoint);
				break;
			}
			case LoopMode::Double:
			{
				renderDoubleLoop(songRenderer, callbackData,
					leftBuffer, rightBuffer, bufferIndex, encoder,
					loopPoint, samplePosition, lastTempo, lastTempoSample);
				break;
			}
			default:
				throw std::runtime_error("Attempted to loop with invalid loop mode " + std::to_string(static_cast<int>(m_loopMode)));
			}
		}
	}

	void MIDIVorbisRenderer::renderShortLoop(SongRenderContainer& songRenderer, float* leftBuffer, float* rightBuffer, size_t& bufferIndex, OggVorbisEncoder& encoder, uint64_t loopStartSample, size_t overlapSamples, uint64_t& samplePosition, uint64_t& loopPoint)
	{
		songRenderer.startPlayback();
		flushBuffersToEncoder(leftBuffer, rightBuffer, bufferIndex, encoder);

		// Just jumping to the loop point seems to create an unavoidable pop when the rendered file's loop point is reached
		// (the short loop mode end, not the song loop point) but synthesizing up to the song loop point and throwing
		// the result away seems to loop just fine...
		// If the start of the loop point is less than one synth buffer into the song, we don't have the granularity
		// to render to that point so we skip this step.
		int bufferSize = songRenderer.getSynthBufferSize();

		if (bufferSize < loopStartSample)
		{
			uint64_t samplesToLoopPoint = loopStartSample - bufferSize;
			float throwawayBuffer = 0;
			songRenderer.renderFrames(samplesToLoopPoint, &throwawayBuffer, &throwawayBuffer, 0);
		}
		songRenderer.silence();
		songRenderer.flushSynthBuffer();

		for (int i = 0; i < overlapSamples; i++)
		{
			readSampleFromSynth(songRenderer, leftBuffer, rightBuffer, bufferIndex, encoder);
		}

		samplePosition += overlapSamples;
		loopPoint += overlapSamples;

		flushBuffersToEncoder(leftBuffer, rightBuffer, bufferIndex, encoder);

		// Synthesizing a little bit extra helps prevent a small pop, click or other
		// looping artifact caused by Vorbis' lossy encoding. See the Vorbis documentation
		// for more information: https://xiph.org/vorbis/doc/vorbisfile/crosslap.html
		// The main idea: synthesizing an extra 64 samples will help Vorbis decoders
		// avoid a pop when looping. This only helps for applications that use Vorbis
		// lapping when playing Vorbis files, which RPG Maker MV (through the Chromium
		// implementation of the Web Audio API) doesn't seem to use.

		// Additionally, this might also give the compression some more information to place
		// the very last sample in the right spot, which might prevent a very, very tiny click
		// from the last sample not quite fitting

		encoder.startOverlapRegion();

		for (int i = 0; i < 64; i++)
		{
			readSampleFromSynth(songRenderer, leftBuffer, rightBuffer, bufferIndex, encoder);
		}
		flushBuffersToEncoder(leftBuffer, rightBuffer, bufferIndex, encoder);

		encoder.endOverlapRegion();

		songRenderer.stopPlayback();
	}

	void MIDIVorbisRenderer::renderDoubleLoop(SongRenderContainer& songRenderer, PlayerCallbackData& callbackData, float* leftBuffer, float* rightBuffer, size_t& bufferIndex, OggVorbisEncoder& encoder, uint64_t& loopPoint, uint64_t& samplePosition, int& lastTempo, uint64_t& lastTempoSample)
	{
		callbackData.m_queuedSeek = callbackData.m_loopTick;
		loopPoint = samplePosition;
		songRenderer.startPlayback();
		while (songRenderer.getIsPlaying())
		{
			readSampleFromSynth(songRenderer, leftBuffer, rightBuffer, bufferIndex, encoder);

			int tempo = songRenderer.getTempo();
			if (tempo != lastTempo)
			{
				lastTempo = tempo;
				lastTempoSample = samplePosition;
			}
			samplePosition++;
		}

		songRenderer.join();

		renderToBeatDivision(songRenderer, samplePosition, lastTempoSample, lastTempo, leftBuffer, rightBuffer, bufferIndex, encoder);

		flushBuffersToEncoder(leftBuffer, rightBuffer, bufferIndex, encoder);
	}

	void MIDIVorbisRenderer::renderToBeatDivision(SongRenderContainer& songRenderer, uint64_t& samplePosition, uint64_t lastTempoSample, int lastTempo, float * leftBuffer, float * rightBuffer, size_t & bufferIndex, OggVorbisEncoder & encoder)
	{
		if (m_endingBeatDivision == -1) { return; }

		uint64_t samplesSinceTempoChange = samplePosition - lastTempoSample;
		double alignmentTempo = 4.0 / m_endingBeatDivision * (lastTempo / 1000000.0);
		double samplesPerAlignedBeat = 44100.0 * (alignmentTempo);
		double beatsElapsed = samplesSinceTempoChange / samplesPerAlignedBeat;
		uint64_t lastAlignedBeat = static_cast<uint64_t>(beatsElapsed) + 1.0;
		uint64_t lastSample = static_cast<uint64_t>(lastAlignedBeat * samplesPerAlignedBeat);
		for (int i = samplePosition; i < lastSample; i++)
		{
			readSampleFromSynth(songRenderer, leftBuffer, rightBuffer, bufferIndex, encoder);
			samplePosition++;
		}
	}

	void MIDIVorbisRenderer::readSampleFromSynth(SongRenderContainer& songRenderer, float* leftBuffer, float* rightBuffer, size_t& bufferIndex, OggVorbisEncoder& encoder)
	{
		songRenderer.renderFrames(1, &leftBuffer[bufferIndex], &rightBuffer[bufferIndex]);

		bufferIndex++;
		if (bufferIndex >= s_audioBufferSize)
		{
			bufferIndex -= s_audioBufferSize;
			encoder.writeBuffers(leftBuffer, rightBuffer, s_audioBufferSize);
		}
	}

	void MIDIVorbisRenderer::flushBuffersToEncoder(float* leftBuffer, float* rightBuffer, size_t& bufferLength, OggVorbisEncoder& encoder)
	{
		if (bufferLength > 0)
		{
			encoder.writeBuffers(leftBuffer, rightBuffer, bufferLength);
			bufferLength = 0;
		}
	}

	void MIDIVorbisRenderer::loadMIDIFile(std::string& fileName, fluid_player_t* player)
	{
#ifndef WINDOWS_UTF16_WORKAROUND
		fluid_player_add(player, fileName.c_str());
#else
		size_t filenameSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, fileName.c_str(), -1, NULL, 0);
		auto filenameString = std::make_unique<wchar_t[]>(filenameSize);
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, fileName.c_str(), -1, filenameString.get(), filenameSize);

		std::ifstream file(filenameString.get(), std::ios_base::in | std::ios_base::binary | std::ios_base::ate);
		if (!file.is_open())
		{
			throw std::invalid_argument("Failed to open MIDI file at " + fileName);
		}

		std::streampos fileLength = file.tellg();
		file.seekg(0, std::ios_base::beg);
		auto fileContents = std::make_unique<char[]>(fileLength);
		file.read(fileContents.get(), fileLength);

		fluid_player_add_mem(player, fileContents.get(), fileLength);
#endif
	}

	int MIDIVorbisRenderer::playerEventCallback(fluid_player_t* player, fluid_synth_t* synth,
		void* data, fluid_midi_event_t* event)
	{
		PlayerCallbackData* callbackData = static_cast<PlayerCallbackData*>(data);

		if (callbackData->m_queuedSeek != -1)
		{
			fluid_player_seek(player, callbackData->m_queuedSeek);
			callbackData->m_queuedSeek = -1;
			return FLUID_OK;
		}

		int eventCode = fluid_midi_event_get_type(event);
		int eventControl = fluid_midi_event_get_control(event);

		if (eventCode == 0xb0 && eventControl == 111)
		{
			callbackData->m_hasHitLoopPoint = true;
			callbackData->m_loopTick = fluid_player_get_current_tick(player);
		}

		return fluid_synth_handle_midi_event(synth, event);
	}
}
