#include <exception>
#include <functional>
#include <filesystem>
#include <iostream>
#include <random>
#include <ctime>
#include <string>
#include <sstream>
#include <fstream>
#include <vector>

#include <fluidsynth.h>

#include "platformsupport.h"
#include "platformchar.h"

#include "cxxopts.hpp"
#include "pathresolution.h"
#include "platformargswrapper.h"
#include "oggvorbisencoder.h"

#define BUFFER_FRAMES_COUNT 1024

struct CallbackData
{
	fluid_player_t* m_player;
	fluid_synth_t* m_synth;
	bool m_isLoopingInFile;
	int m_loopTick;
	int m_queuedSeek;
	int m_queuedSeekLaunchTick;
	bool m_hasHitLoopPoint;
};

int playerEventCallback(void* data, fluid_midi_event_t* event);
bool renderSong(fluid_synth_t* synth, CallbackData& data, std::string fileName, int endingBeatDivision, OggVorbisEncoder& outputFile, bool& hasLoopPoint, uint64_t& loopPoint, uint64_t& sampleCount);

using namespace midirenderer;

int MAIN(int argc, argv_t** argv)
{
	PlatformArgsWrapper wrapper(argc, argv);
	char** wrappedArgv = wrapper.getArgv();
	cxxopts::Options options(wrappedArgv[0], "  A midi to RPGMV-compatible looping OGG converter");

	options.positional_help("<files> -f <soundfont>");
	options.add_options()
		("help", "Show this help document")
		("files", "The midi file(s) to convert", cxxopts::value<std::vector<std::string>>())
		("f,soundfont", "(Required) The path of the soundfont to use", cxxopts::value<std::string>(), "soundfont.sf2")
		("d,destination", "The folder to place the rendered files in", cxxopts::value<std::string>(), "output")
		("loop", "Render the audio looped to help make the loop more seamless at the cost of filesize")
		("end-on-division", "Align the end of the song to a note division up to a 64th note",
			cxxopts::value<int>(), "4");
	options.parse_positional({ "files" });

	std::unique_ptr<cxxopts::ParseResult> parsedArgsPtr;
	try
	{
		parsedArgsPtr = std::make_unique<cxxopts::ParseResult>(std::move(options.parse(argc, wrappedArgv)));
	}
	catch (cxxopts::OptionException e)
	{
		std::cout << "Invalid input arguments: " << e.what() << std::endl <<
			options.help() << std::endl;
		return 1;
	}

	cxxopts::ParseResult& parsedArgs = *parsedArgsPtr;

	if (parsedArgs.count("help") > 0)
	{
		std::cout << options.help() << std::endl;
		return 0;
	}

	bool isLoopingInFile = parsedArgs.count("loop") > 0;
	
	int beatDivision = -1;
	if (parsedArgs.count("end-on-division") == 1)
	{
		beatDivision = parsedArgs["end-on-division"].as<int>();

		// (n & (n - 1)) == 0 is a clever power of two check courtesy of this answer of StackOverflow
		// https://stackoverflow.com/a/600306
		// https://web.archive.org/web/20160914012004/http://stackoverflow.com/questions/600293/how-to-check-if-a-number-is-a-power-of-2/600306
		if (beatDivision <= 0 || beatDivision > 64 || (beatDivision & (beatDivision - 1)) != 0)
		{
			std::cout << "Invalid beat division " << beatDivision << " given - please use a power of two beat division from 1 (whole note) to 64" << std::endl <<
				options.help() << std::endl;
			return 1;
		}
	}
	else if (parsedArgs.count("end-on-division") > 1)
	{
		std::cout << "Error: Argument \"end-on-division\" is given multiple times" << std::endl <<
			options.help() << std::endl;
		return 1;
	}

	std::string soundfontPath;
	if (parsedArgs.count("f") > 0)
	{
		soundfontPath = parsedArgs["f"].as<std::string>();
#ifndef WINDOWS_UTF16_WORKAROUND
		if (!fluid_is_soundfont(soundfontPath.c_str()))
		{
			std::cout << "The soundfont at " << soundfontPath.c_str() << " is missing or invalid" << std::endl;
			return 1;
		}
#endif
	}
	else
	{
		std::cout << "No soundfont specified - use -f <path> or --soundfont <path>" << std::endl <<
			options.help() << std::endl;
		return 1;
	}

	std::filesystem::path outputFolder;
	if (parsedArgs.count("d") > 0)
	{
		std::string folderArg = parsedArgs["d"].as<std::string>();
		std::filesystem::path path = std::filesystem::u8path(folderArg);
		if (std::filesystem::exists(path) && std::filesystem::is_directory(folderArg))
		{
			outputFolder = path.lexically_normal();
		}
	}

	std::vector<std::string> midiFiles;
	std::vector<std::string> outputFiles;
	if (parsedArgs.count("files") > 0)
	{
		const std::vector<std::string>& midiPaths = parsedArgs["files"].as<std::vector<std::string>>();
		for (const auto& path : midiPaths)
		{
			int midiFileCount = midiFiles.size();
			utils::resolveWildcardedPath(path, [&](std::string path)
			{
				const char* filename = path.c_str();
#ifndef WINDOWS_UTF16_WORKAROUND
				if (!fluid_is_midifile(filename))
				{
					return;
				}
#endif
				midiFiles.push_back(path);

				std::filesystem::path oggPath = std::filesystem::u8path(path);
				oggPath.replace_extension(".ogg");
				if (!outputFolder.empty())
				{
					oggPath = outputFolder / oggPath.filename();
				}
				outputFiles.push_back(oggPath.u8string());
			});

			if (midiFiles.size() == midiFileCount)
			{
				std::cout << "No midi file(s) found at " << path << "; skipping" << std::endl;
			}
		}
	}
	
	if (midiFiles.size() == 0)
	{
		std::cout << "No valid midi files specified." << std::endl <<
			options.help() << std::endl;
		return 1;
	}

    fluid_settings_t* settings = new_fluid_settings();
	std::shared_ptr<fluid_settings_t> settingsWrapper(settings, delete_fluid_settings);

	fluid_settings_setnum(settings, "synth.sample-rate", 44100.0);
	fluid_settings_setint(settings, "synth.chorus.active", 0);
	fluid_settings_setint(settings, "synth.reverb.active", 0);
	fluid_settings_setnum(settings, "synth.gain", 0.5);
	fluid_settings_setstr(settings, "player.timing-source", "sample");
	// Don't reset just in case stopping and starting resets it - we want playback to be seamless
	fluid_settings_setint(settings, "player.reset-synth", 0);

	// From the docs: "since this is a non-realtime scenario, there is no need to pin the sample data"
	fluid_settings_setint(settings, "synth.lock-memory", 0);

	fluid_synth_t* synth = new_fluid_synth(settings);
	std::shared_ptr<fluid_synth_t> synthWrapper(synth, delete_fluid_synth);

	int soundfontLoadResult = fluid_synth_sfload(synth, soundfontPath.c_str(), 1);
	if (soundfontLoadResult == FLUID_FAILED)
	{
		std::cout << "Failed to load the soundfont at " << soundfontPath.c_str() << "; aborting" << std::endl;
		return 1;
	}

	for (int i = 0; i < midiFiles.size(); i++)
	{
		std::default_random_engine rng;
		rng.seed(time(NULL));
		OggVorbisEncoder encoder = OggVorbisEncoder(static_cast<int>(rng()), 44100, 0.4);

		fluid_synth_all_sounds_off(synth, -1);
		std::cout << "Rendering " << midiFiles[i] << "..." << std::endl;

		CallbackData data = { nullptr, synth, isLoopingInFile, -1, -1, -1, false };
		uint64_t samplePosition = 0;
		bool hasLoopPoint = false;
		uint64_t loopPoint = 0;

		bool didRenderSucceed = renderSong(synth, data, midiFiles[i], beatDivision, encoder, hasLoopPoint, loopPoint, samplePosition);
		if (!didRenderSucceed) return 1;
		if (isLoopingInFile)
		{
			didRenderSucceed = renderSong(synth, data, midiFiles[i], beatDivision, encoder, hasLoopPoint, loopPoint, samplePosition);
			if (!didRenderSucceed) return 1;
		}

		encoder.addComment("ENCODER", "libvorbis (midirenderer)");
		if (hasLoopPoint)
		{
			encoder.addComment("LOOPSTART", std::to_string(loopPoint));
			encoder.addComment("LOOPLENGTH", std::to_string(samplePosition - loopPoint));
		}

		std::ofstream fileOutput;
		fileOutput.open(stringutils::getPlatformString(outputFiles[i]), std::ios_base::out | std::ios_base::binary);
		auto pageCallback = [&](const unsigned char* header, long headerLength, const unsigned char* body, long bodyLength)
		{
			fileOutput.write(reinterpret_cast<const char*>(header), headerLength);
			fileOutput.write(reinterpret_cast<const char*>(body), bodyLength);
		};

		encoder.readHeader(pageCallback);
		encoder.completeStream(pageCallback);
	}

    return 0;
}

bool renderSong(fluid_synth_t* synth, CallbackData& data, std::string fileName, int endingBeatDivision, OggVorbisEncoder& encoder, bool& hasLoopPoint, uint64_t& loopPoint, uint64_t& samplePosition)
{
	fluid_player_t* player = new_fluid_player(synth);
	std::shared_ptr<fluid_player_t> playerWrapper(player, delete_fluid_player);

	data.m_player = player;
	fluid_player_set_playback_callback(player, playerEventCallback, static_cast<void*>(&data));

#ifndef WINDOWS_UTF16_WORKAROUND
	fluid_player_add(player, fileName.c_str());
#else
	size_t filenameSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, fileName.c_str(), -1, NULL, 0);
	auto filenameString = std::make_unique<wchar_t[]>(filenameSize);
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, fileName.c_str(), -1, filenameString.get(), filenameSize);

	std::shared_ptr<FILE> file(_wfopen(filenameString.get(), L"rb"), fclose);

	if (file.get() == NULL)
	{
		std::cout << L"Failed to open MIDI file at " << filenameString.get() << L"; skipping" << std::endl;
		return false;
	}

	_fseeki64(file.get(), 0LL, SEEK_END);
	__int64 fileLength = _ftelli64(file.get());
	rewind(file.get());
	auto fileContents = std::make_unique<char[]>(fileLength);

	fread_s(fileContents.get(), fileLength, sizeof(char), fileLength, file.get());

	fluid_player_add_mem(player, fileContents.get(), fileLength);
	file.reset();
#endif

	fluid_player_play(player);

	float leftBuffer[BUFFER_FRAMES_COUNT];
	float rightBuffer[BUFFER_FRAMES_COUNT];
	size_t bufferIndex = 0;

	int lastTempo = fluid_player_get_midi_tempo(player);
	uint64_t lastTempoSample = samplePosition;

	while (fluid_player_get_status(player) == FLUID_PLAYER_PLAYING)
	{
		if (fluid_synth_write_float(synth, 1, leftBuffer, bufferIndex, 1, rightBuffer, bufferIndex, 1))
		{
			std::cout << "Synth encountered an error" << std::endl;
			return false;
		}
		bufferIndex++;
		if (bufferIndex >= BUFFER_FRAMES_COUNT)
		{
			bufferIndex -= BUFFER_FRAMES_COUNT;
			encoder.writeBuffers(leftBuffer, rightBuffer, BUFFER_FRAMES_COUNT);
		}

		if (!hasLoopPoint && data.m_hasHitLoopPoint)
		{
			hasLoopPoint = true;
			loopPoint = samplePosition;
		}

		int tempo = fluid_player_get_midi_tempo(player);
		if (tempo != lastTempo)
		{
			lastTempo = tempo;
			lastTempoSample = samplePosition;
		}
		samplePosition++;
	}

	fluid_player_join(player);

	if (endingBeatDivision != -1)
	{
		uint64_t samplesSinceTempoChange = samplePosition - lastTempoSample;
		double alignmentTempo = 4.0 / endingBeatDivision * (lastTempo / 1000000.0);
		double samplesPerAlignedBeat = 44100.0 * (alignmentTempo);
		double beatsElapsed = samplesSinceTempoChange / samplesPerAlignedBeat;
		uint64_t lastAlignedBeat = static_cast<uint64_t>(beatsElapsed) + 1.0;
		uint64_t lastSample = static_cast<uint64_t>(lastAlignedBeat * samplesPerAlignedBeat);
		for (int j = samplePosition; j <= lastSample; j++)
		{
			if (fluid_synth_write_float(synth, 1, leftBuffer, bufferIndex, 1, rightBuffer, bufferIndex, 1))
			{
				std::cout << "Synth encountered an error" << std::endl;
				return false;
			}
			bufferIndex++;
			if (bufferIndex >= BUFFER_FRAMES_COUNT)
			{
				bufferIndex -= BUFFER_FRAMES_COUNT;
				encoder.writeBuffers(leftBuffer, rightBuffer, BUFFER_FRAMES_COUNT);
			}
			samplePosition++;
		}
	}

	// Play the voice runoff of the end, which may or may not end up part of the loop

	// samplePosition isn't incremented here because it's used to determine loop points
	// and the runoff is not meant to delay the loop point at the end of the song.
	encoder.startOverlapRegion();
	while (fluid_synth_get_active_voice_count(synth) > 0)
	{
		if (fluid_synth_write_float(synth, 1, leftBuffer, bufferIndex, 1, rightBuffer, bufferIndex, 1))
		{
			std::cout << "Synth encountered an error" << std::endl;
			return false;
		}
		bufferIndex++;
		if (bufferIndex >= BUFFER_FRAMES_COUNT)
		{
			bufferIndex -= BUFFER_FRAMES_COUNT;
			encoder.writeBuffers(leftBuffer, rightBuffer, BUFFER_FRAMES_COUNT);
		}
	}

	if (bufferIndex != 0)
	{
		encoder.writeBuffers(leftBuffer, rightBuffer, bufferIndex);
	}
	encoder.endOverlapRegion();

	return true;
}

int playerEventCallback(void* data, fluid_midi_event_t* event)
{
    CallbackData* callbackData = static_cast<CallbackData*>(data);

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
