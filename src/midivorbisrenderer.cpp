#include "midivorbisrenderer.h"

#include <exception>
#include <fstream>
#include <random>
#include <ctime>

#include <fluidsynth.h>

#include "platformsupport.h"
#include "oggvorbisencoder.h"

namespace midirenderer
{
	struct PlayerCallbackData
	{
		fluid_player_t* m_player;
		fluid_synth_t* m_synth;
		bool m_isLoopingInFile;
		int m_loopTick;
		int m_queuedSeek;
		int m_queuedSeekLaunchTick;
		bool m_hasHitLoopPoint;

		PlayerCallbackData(fluid_player_t* player, fluid_synth_t* synth, bool isLoopingInFile) :
			m_player(player), m_synth(synth), m_isLoopingInFile(isLoopingInFile),
			m_loopTick(-1), m_queuedSeek(-1), m_hasHitLoopPoint(false) { }
	};

	MIDIVorbisRenderer::MIDIVorbisRenderer(std::string soundfontPath, bool isLoopingInFile, int endingBeatDivision) :
		m_isLoopingInFile(isLoopingInFile), m_endingBeatDivision(endingBeatDivision),
		m_fluidSettings(nullptr, nullptr),
		m_fluidSynth(nullptr, nullptr)
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

		m_fluidSynth = deleter_unique_ptr<fluid_synth_t>(new_fluid_synth(m_fluidSettings.get()), delete_fluid_synth);
		int soundfontLoadResult = fluid_synth_sfload(m_fluidSynth.get(), soundfontPath.c_str(), 1);
		if (soundfontLoadResult == FLUID_FAILED)
		{
			throw std::invalid_argument("Failed to load the soundfont at " + soundfontPath);
		}
	}

	bool MIDIVorbisRenderer::renderFile(std::string sourcePath, std::string outputPath)
	{
		std::default_random_engine rng;
		rng.seed(time(NULL));
		OggVorbisEncoder encoder = OggVorbisEncoder(static_cast<int>(rng()), 44100, 0.4);

		PlayerCallbackData callbackData(nullptr, m_fluidSynth.get(), m_isLoopingInFile);
		uint64_t samplePosition = 0;
		bool hasLoopPoint = false;
		uint64_t loopPoint = 0;

		renderSong(callbackData, sourcePath, encoder, hasLoopPoint, loopPoint, samplePosition);
		if (m_isLoopingInFile)
		{
			renderSong(callbackData, sourcePath, encoder, hasLoopPoint, loopPoint, samplePosition);
		}

		fluid_synth_all_sounds_off(m_fluidSynth.get(), -1);

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

		return true;
	}

	void MIDIVorbisRenderer::renderSong(PlayerCallbackData& callbackData, std::string fileName, OggVorbisEncoder& encoder, bool& hasLoopPoint, uint64_t& loopPoint, uint64_t& samplePosition)
	{
		deleter_unique_ptr<fluid_player_t> player(new_fluid_player(m_fluidSynth.get()), delete_fluid_player);

		callbackData.m_player = player.get();
		fluid_player_set_playback_callback(player.get(), playerEventCallback, static_cast<void*>(&callbackData));

#ifndef WINDOWS_UTF16_WORKAROUND
		fluid_player_add(player.get(), fileName.c_str());
#else
		size_t filenameSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, fileName.c_str(), -1, NULL, 0);
		auto filenameString = std::make_unique<wchar_t[]>(filenameSize);
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, fileName.c_str(), -1, filenameString.get(), filenameSize);

		std::shared_ptr<FILE> file(_wfopen(filenameString.get(), L"rb"), fclose);

		if (file.get() == NULL)
		{
			throw std::invalid_argument("Failed to open MIDI file at " + fileName);
		}

		_fseeki64(file.get(), 0LL, SEEK_END);
		__int64 fileLength = _ftelli64(file.get());
		rewind(file.get());
		auto fileContents = std::make_unique<char[]>(fileLength);

		fread_s(fileContents.get(), fileLength, sizeof(char), fileLength, file.get());

		fluid_player_add_mem(player.get(), fileContents.get(), fileLength);
		file.reset();
#endif

		fluid_player_play(player.get());

		float leftBuffer[s_audioBufferSize];
		float rightBuffer[s_audioBufferSize];
		size_t bufferIndex = 0;

		int lastTempo = fluid_player_get_midi_tempo(player.get());
		uint64_t lastTempoSample = samplePosition;

		while (fluid_player_get_status(player.get()) == FLUID_PLAYER_PLAYING)
		{
			readSampleFromSynth(leftBuffer, rightBuffer, bufferIndex, encoder);

			if (!hasLoopPoint && callbackData.m_hasHitLoopPoint)
			{
				hasLoopPoint = true;
				loopPoint = samplePosition;
			}

			int tempo = fluid_player_get_midi_tempo(player.get());
			if (tempo != lastTempo)
			{
				lastTempo = tempo;
				lastTempoSample = samplePosition;
			}
			samplePosition++;
		}

		fluid_player_join(player.get());

		if (m_endingBeatDivision != -1)
		{
			uint64_t samplesSinceTempoChange = samplePosition - lastTempoSample;
			double alignmentTempo = 4.0 / m_endingBeatDivision * (lastTempo / 1000000.0);
			double samplesPerAlignedBeat = 44100.0 * (alignmentTempo);
			double beatsElapsed = samplesSinceTempoChange / samplesPerAlignedBeat;
			uint64_t lastAlignedBeat = static_cast<uint64_t>(beatsElapsed) + 1.0;
			uint64_t lastSample = static_cast<uint64_t>(lastAlignedBeat * samplesPerAlignedBeat);
			for (int j = samplePosition; j <= lastSample; j++)
			{
				readSampleFromSynth(leftBuffer, rightBuffer, bufferIndex, encoder);
				samplePosition++;
			}
		}

		// Play the voice runoff of the end, which may or may not end up part of the loop

		// samplePosition isn't incremented here because it's used to determine loop points
		// and the runoff is not meant to delay the loop point at the end of the song.
		encoder.startOverlapRegion();
		while (fluid_synth_get_active_voice_count(m_fluidSynth.get()) > 0)
		{
			readSampleFromSynth(leftBuffer, rightBuffer, bufferIndex, encoder);
		}

		if (bufferIndex != 0)
		{
			encoder.writeBuffers(leftBuffer, rightBuffer, bufferIndex);
		}
		encoder.endOverlapRegion();
	}

	void MIDIVorbisRenderer::readSampleFromSynth(float* leftBuffer, float* rightBuffer, size_t& bufferIndex, OggVorbisEncoder& encoder)
	{
		if (fluid_synth_write_float(m_fluidSynth.get(), 1, leftBuffer, bufferIndex, 1, rightBuffer, bufferIndex, 1))
		{
			throw std::runtime_error("Synth encountered an error");
		}
		bufferIndex++;
		if (bufferIndex >= s_audioBufferSize)
		{
			bufferIndex -= s_audioBufferSize;
			encoder.writeBuffers(leftBuffer, rightBuffer, s_audioBufferSize);
		}
	}

	int MIDIVorbisRenderer::playerEventCallback(void* data, fluid_midi_event_t* event)
	{
		PlayerCallbackData* callbackData = static_cast<PlayerCallbackData*>(data);

		int currentTick = fluid_player_get_current_tick(callbackData->m_player);

		if (callbackData->m_queuedSeek != -1 &&
			currentTick < callbackData->m_queuedSeekLaunchTick)
		{
			fluid_player_seek(callbackData->m_player, callbackData->m_queuedSeek);
			callbackData->m_queuedSeek = -1;
			return FLUID_OK;
		}

		int eventCode = fluid_midi_event_get_type(event);
		int eventControl = fluid_midi_event_get_control(event);

		if (eventCode == 0xb0 && eventControl == 111)
		{
			if (callbackData->m_isLoopingInFile)
			{
				callbackData->m_loopTick = currentTick;
			}
			else
			{
				callbackData->m_hasHitLoopPoint = true;
			}
		}

		if (callbackData->m_queuedSeek == -1 &&
			callbackData->m_loopTick >= 0 &&
			currentTick == fluid_player_get_total_ticks(callbackData->m_player))
		{
			if (callbackData->m_isLoopingInFile)
			{
				callbackData->m_queuedSeek = callbackData->m_loopTick;
				callbackData->m_queuedSeekLaunchTick = currentTick;
				callbackData->m_hasHitLoopPoint = true;
			}
		}
		return fluid_synth_handle_midi_event(callbackData->m_synth, event);
	}
}
