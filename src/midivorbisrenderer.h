#pragma once

#include <string>
#include <memory>

#include <fluidsynth/types.h>

class OggVorbisEncoder;

namespace midirenderer
{
	struct PlayerCallbackData;

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
		template<typename T>
		using deleter_t = void(*)(T*);

		template<typename T>
		using deleter_unique_ptr = std::unique_ptr<T, deleter_t<T>>;

		void renderSong(PlayerCallbackData& callbackData, std::string fileName, OggVorbisEncoder& encoder, bool& hasLoopPoint, uint64_t& loopPoint, uint64_t& samplePosition);

		void readSampleFromSynth(float* leftBuffer, float* rightBuffer, size_t& bufferIndex, OggVorbisEncoder& encoder);
		void clearSynthBuffer();
		void flushBuffersToEncoder(float* leftBuffer, float* rightBuffer, size_t& bufferLength, OggVorbisEncoder& encoder);

		static void loadMIDIFile(std::string &fileName, fluid_player_t* loopPlayer);
		static int playerEventCallback(void* data, fluid_midi_event_t* event);

		LoopMode m_loopMode;
		int m_endingBeatDivision;

		deleter_unique_ptr<fluid_settings_t> m_fluidSettings;
		deleter_unique_ptr<fluid_synth_t> m_synth;

		size_t m_framesSinceSynthClear;

		constexpr static size_t s_audioBufferSize = 1024;
		constexpr static size_t s_loopClickBufferSize = 128;
	};
}
