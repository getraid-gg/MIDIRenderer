#include "songrendercontainer.h"

#include <fstream>
#include <fluidsynth.h>
#include <iostream>

#include "platformsupport.h"

namespace midirenderer
{
	SongRenderContainer::SongRenderContainer(std::string fileName, fluid_sfont_t* soundfont) :
		m_fileName(fileName),
		m_settings(nullptr, &SongRenderContainer::deleteFluidSettings),
		m_synth(nullptr, &SongRenderContainer::deleteSynth),
		m_player(nullptr, &SongRenderContainer::deletePlayer),
		m_synthBufferPosition(0)
	{
		m_settings.reset(new_fluid_settings());

		fluid_settings_setnum(m_settings.get(), "synth.sample-rate", 44100.0);
		fluid_settings_setint(m_settings.get(), "synth.chorus.active", 0);
		fluid_settings_setint(m_settings.get(), "synth.reverb.active", 0);
		fluid_settings_setnum(m_settings.get(), "synth.gain", 0.5);
		fluid_settings_setstr(m_settings.get(), "player.timing-source", "sample");
		// Don't reset just in case stopping and starting resets it - we want playback to be seamless
		fluid_settings_setint(m_settings.get(), "player.reset-synth", 0);
		// From the docs: "since this is a non-realtime scenario, there is no need to pin the sample data"
		fluid_settings_setint(m_settings.get(), "synth.lock-memory", 0);

		m_synth.reset(new_fluid_synth(m_settings.get()));
		fluid_synth_add_sfont(m_synth.get(), soundfont);
		m_synthBufferSize = fluid_synth_get_internal_bufsize(m_synth.get());

		m_midiCallbackData = { m_synth.get(), m_player.get(), nullptr, nullptr };
		resetPlayer();
	}

	int SongRenderContainer::getTempo()
	{
		return fluid_player_get_midi_tempo(m_player.get());
	}

	int SongRenderContainer::getSynthBufferSize()
	{
		return m_synthBufferSize;
	}

	void SongRenderContainer::setMIDICallback(FluidsynthMIDIMessageHandler eventCallback, void* callbackData)
	{
		m_midiCallbackData.m_userCallback = eventCallback;
		m_midiCallbackData.m_userCallbackData = callbackData;
		refreshMIDICallback();
	}

	void SongRenderContainer::startPlayback()
	{
		fluid_player_play(m_player.get());
		flushSynthBuffer();
	}

	void SongRenderContainer::stopPlayback()
	{
		fluid_player_stop(m_player.get());
	}

	void SongRenderContainer::join()
	{
		fluid_player_join(m_player.get());
	}

	void SongRenderContainer::silence()
	{
		fluid_synth_all_notes_off(m_synth.get(), -1);
	}

	void SongRenderContainer::resetPlayer()
	{
		if (m_player != nullptr)
		{
			fluid_player_stop(m_player.get());
		}
		m_player.reset(new_fluid_player(m_synth.get()));
		loadMIDIFile();
		m_midiCallbackData.m_player = m_player.get();
		refreshMIDICallback();
	}

	void SongRenderContainer::renderFrames(int count, float* leftBuffer, float* rightBuffer, int increment)
	{
		if (fluid_synth_write_float(m_synth.get(), count, leftBuffer, 0, increment, rightBuffer, 0, increment))
		{
			throw std::runtime_error("Synth encountered an error");
		}

		m_synthBufferPosition = (m_synthBufferPosition + count) % m_synthBufferSize;
	}

	bool SongRenderContainer::getIsPlaying()
	{
		return fluid_player_get_status(m_player.get()) == FLUID_PLAYER_PLAYING;
	}

	int SongRenderContainer::getActiveVoiceCount()
	{
		return fluid_synth_get_active_voice_count(m_synth.get());
	}

	void SongRenderContainer::loadMIDIFile()
	{
#ifndef WINDOWS_UTF16_WORKAROUND
		fluid_player_add(m_player.get(), m_fileName.c_str());
#else
		size_t filenameSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, m_fileName.c_str(), -1, NULL, 0);
		auto filenameString = std::make_unique<wchar_t[]>(filenameSize);
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, m_fileName.c_str(), -1, filenameString.get(), filenameSize);

		std::ifstream file(filenameString.get(), std::ios_base::in | std::ios_base::binary | std::ios_base::ate);
		if (!file.is_open())
		{
			throw std::invalid_argument("Failed to open MIDI file at " + m_fileName);
		}

		std::streampos fileLength = file.tellg();
		file.seekg(0, std::ios_base::beg);
		auto fileContents = std::make_unique<char[]>(fileLength);
		file.read(fileContents.get(), fileLength);

		fluid_player_add_mem(m_player.get(), fileContents.get(), fileLength);
#endif
	}

	void SongRenderContainer::refreshMIDICallback()
	{
		if (m_midiCallbackData.m_userCallback != nullptr)
		{
			fluid_player_set_playback_callback(m_player.get(), &SongRenderContainer::onMIDIEvent, &m_midiCallbackData);
		}
		else
		{
			fluid_player_set_playback_callback(m_player.get(), &fluid_synth_handle_midi_event, NULL);
		}
	}

	void SongRenderContainer::deleteSynth(fluid_synth_t* synth)
	{
		if (synth == nullptr) { return; }

		// Before deleting the synth, we need to remove the soundfont, since FluidSynth seems to
		// delete all of a deleted synth's soundfonts, regardless of whether or not the soundfont
		// is in use on other synths.
		// Before doing that, we need to unset the programs on all of the channels or else
		// the console will get a warning message for every channel when it tries to reassign
		// instruments to the channels and fails since there's no fallback synth
		int channelCount = fluid_synth_count_midi_channels(synth);
		for (int i = 0; i < channelCount; i++)
		{
			fluid_synth_unset_program(synth, i);
		}

		int soundfontCount = fluid_synth_sfcount(synth);
		for (int i = soundfontCount - 1; i >= 0; i--)
		{
			fluid_synth_remove_sfont(synth, fluid_synth_get_sfont(synth, i));
		}

		delete_fluid_synth(synth);
	}

	void SongRenderContainer::deletePlayer(fluid_player_t* player)
	{
		if (player != nullptr)
		{
			delete_fluid_player(player);
		}
	}

	void SongRenderContainer::deleteFluidSettings(fluid_settings_t* settings)
	{
		if (settings != nullptr)
		{
			delete_fluid_settings(settings);
		}
	}

	void SongRenderContainer::flushSynthBuffer()
	{
		float buffer = 0;
		fluid_synth_write_float(m_synth.get(), m_synthBufferSize - m_synthBufferPosition, &buffer, 0, 0, &buffer, 0, 0);
		m_synthBufferPosition = 0;
	}

	int SongRenderContainer::onMIDIEvent(void* data, fluid_midi_event_t* eventData)
	{
		CallbackData* callbackData = static_cast<CallbackData*>(data);

		if (callbackData->m_userCallback == nullptr)
		{
			return fluid_synth_handle_midi_event(callbackData->m_synth, eventData);
		}
		
		auto callback = (callbackData->m_userCallback);

		return callback(callbackData->m_player, callbackData->m_synth, callbackData->m_userCallbackData, eventData);
	}
}
