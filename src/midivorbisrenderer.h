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
		MIDIVorbisRenderer(std::string sondfontPath, bool isLoopingInFile, int endingBeatDivision);

		bool renderFile(std::string sourcePath, std::string outputPath);
	private:
		void renderSong(PlayerCallbackData& callbackData, std::string fileName, OggVorbisEncoder& encoder, bool& hasLoopPoint, uint64_t& loopPoint, uint64_t& samplePosition);
		void readSampleFromSynth(float* leftBuffer, float* rightBuffer, size_t& bufferIndex, OggVorbisEncoder& encoder);
		static int playerEventCallback(void* data, fluid_midi_event_t* event);

		template<typename T>
		using deleter_t = void(*)(T*);

		template<typename T>
		using deleter_unique_ptr = std::unique_ptr<T, deleter_t<T>>;

		bool m_isLoopingInFile;
		int m_endingBeatDivision;

		deleter_unique_ptr<fluid_settings_t> m_fluidSettings;
		deleter_unique_ptr<fluid_synth_t> m_fluidSynth;

		constexpr static size_t s_audioBufferSize = 1024;
	};
}
