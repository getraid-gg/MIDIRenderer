#pragma once

#include <string>
#include <memory>

#include <fluidsynth/types.h>

#include "deleteruniqueptr.h"

class OggVorbisEncoder;

namespace midirenderer
{
	struct PlayerCallbackData;
	class SongRenderContainer;

	class MIDIVorbisRenderer
	{
	public:
		enum class LoopMode
		{
			None,
			Double,
			Short
		};

		MIDIVorbisRenderer(LoopMode loopMode = LoopMode::None, int endingBeatDivision = -1);

		void loadSoundfont(std::string soundfontPath);

		void renderFile(std::string sourcePath, std::string outputPath);

		bool getHasSoundfont();
	private:
		void renderSong(PlayerCallbackData& callbackData, std::string fileName, OggVorbisEncoder& encoder, uint64_t& loopStart, uint64_t& songLength);

		void renderShortLoop(SongRenderContainer& songRenderer, float* leftBuffer, float* rightBuffer, size_t& bufferIndex, OggVorbisEncoder& encoder,
			uint64_t loopStartSample, size_t overlapSamples, uint64_t& samplePosition, uint64_t& loopPoint);

		void renderDoubleLoop(SongRenderContainer& songRenderer, PlayerCallbackData& callbackData,
			float* leftBuffer, float* rightBuffer, size_t& bufferIndex, OggVorbisEncoder& encoder,
			uint64_t& loopPoint, uint64_t& samplePosition, int& lastTempo, uint64_t& lastTempoSample);

		void renderToBeatDivision(SongRenderContainer& songRenderer, uint64_t& samplePosition, uint64_t lastTempoSample, int lastTempo, float* leftBuffer, float* rightBuffer, size_t& bufferIndex, OggVorbisEncoder& encoder);

		void readSampleFromSynth(SongRenderContainer& songRenderer, float* leftBuffer, float* rightBuffer, size_t& bufferIndex, OggVorbisEncoder& encoder);

		void flushBuffersToEncoder(float* leftBuffer, float* rightBuffer, size_t& bufferLength, OggVorbisEncoder& encoder);

		static void loadMIDIFile(std::string &fileName, fluid_player_t* loopPlayer);
		static int playerEventCallback(fluid_player_t* player, fluid_synth_t* synth,
			void* data, fluid_midi_event_t* event);

		LoopMode m_loopMode;
		int m_endingBeatDivision;

		// While this synth isn't used to do any synthesis, this is the only way I see to
		// create a soundfont to share between synth instances
		deleter_unique_ptr<fluid_settings_t> m_fluidSettings;
		deleter_unique_ptr<fluid_synth_t> m_synth;

		constexpr static size_t s_audioBufferSize = 1024;
		constexpr static size_t s_loopClickBufferSize = 128;
	};
}
