#pragma once

#include <string>
#include <functional>

#include <fluidsynth/types.h>

#include "deleteruniqueptr.h"

namespace midirenderer
{
	typedef std::function<int(fluid_player_t* player, fluid_synth_t* synth,
		void* userData, fluid_midi_event_t* eventData)> FluidsynthMIDIMessageHandler;

	class SongRenderContainer
	{
	public:
		SongRenderContainer(std::string fileName, fluid_sfont_t* soundfont);
		
		SongRenderContainer(const SongRenderContainer& other) = delete;
		SongRenderContainer& operator=(const SongRenderContainer& other) = delete;

		int getTempo();
		int getSynthBufferSize();

		void setMIDICallback(FluidsynthMIDIMessageHandler eventCallback, void* callbackData);

		void startPlayback();
		void stopPlayback();
		void join();
		void silence();
		void resetPlayer();

		void renderFrames(int count, float* leftBuffer, float* rightBuffer, int increment = 1);
		void flushSynthBuffer();
		bool getIsPlaying();
		int getActiveVoiceCount();

	private:
		struct CallbackData
		{
			fluid_synth_t* m_synth;
			fluid_player_t* m_player;
			FluidsynthMIDIMessageHandler m_userCallback;
			void* m_userCallbackData;
		};

		void loadMIDIFile();
		void refreshMIDICallback();

		static void deleteSynth(fluid_synth_t* synth);
		static void deletePlayer(fluid_player_t* player);
		static void deleteFluidSettings(fluid_settings_t* settings);

		static int onMIDIEvent(void* data, fluid_midi_event_t* eventData);

		std::string m_fileName;
		deleter_unique_ptr<fluid_settings_t> m_settings;
		deleter_unique_ptr<fluid_synth_t> m_synth;
		deleter_unique_ptr<fluid_player_t> m_player;

		CallbackData m_midiCallbackData;

		int m_synthBufferSize;
		int m_synthBufferPosition;
	};
}