#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#include "platformsupport.h"

#include "cxxopts.hpp"
#include "pathresolution.h"
#include "platformargswrapper.h"
#include "midivorbisrenderer.h"

#ifndef WINDOWS_UTF16_WORKAROUND
#include "fluidsynth.h"
#endif

using namespace midirenderer;

int MAIN(int argc, argv_t** argv)
{
	PlatformArgsWrapper wrapper(argc, argv);
	char** wrappedArgv = wrapper.getArgv();
	cxxopts::Options options(wrappedArgv[0], "  A MIDI to RPGMV-compatible looping OGG converter");

	options.positional_help("<files> -f <soundfont>");
	options.add_options()
		("help", "Show this help document")
		("files", "The midi file(s) to convert", cxxopts::value<std::vector<std::string>>())
		("f,soundfont", "(Required) The path to the soundfont to use", cxxopts::value<std::string>(), "soundfont.sf2")
		("d,destination", "The folder to place the rendered files in", cxxopts::value<std::string>(), "output")
		("loop", "Render the audio looped to help make the loop more seamless at the cost of filesize")
		("loop-mode", "The mode to use when rendering the audio looped (implies --loop)\n"
			"\tshort: (default) after the end of the song, render again from the start of the loop until all voices from the end have terminated (minimal filesize impact)\n"
			"\tdouble: loop the audio twice (cleanest loop)", cxxopts::value<std::string>(), "short|double")
		("end-on-division", "Align the end of the song to a note division up to a 64th note",
			cxxopts::value<int>(), "4");
	options.parse_positional({ "files" });

	std::unique_ptr<cxxopts::ParseResult> parsedArgsPtr;
	try
	{
		parsedArgsPtr = std::make_unique<cxxopts::ParseResult>(std::move(options.parse(argc, wrappedArgv)));
	}
	catch (cxxopts::OptionException& e)
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

	MIDIVorbisRenderer::LoopMode loopMode = MIDIVorbisRenderer::LoopMode::None;
	if (parsedArgs.count("loop-mode") > 0)
	{
		std::string modeString = parsedArgs["loop-mode"].as<std::string>();

		if (modeString == "short")
		{
			loopMode = MIDIVorbisRenderer::LoopMode::Short;
		}
		else if (modeString == "double")
		{
			loopMode = MIDIVorbisRenderer::LoopMode::Double;
		}
		else
		{
			std::cout << "Invalid loop mode" << modeString << std::endl << options.help() << std::endl;
			return 1;
		}
	}

	if (loopMode == MIDIVorbisRenderer::LoopMode::None && parsedArgs.count("loop") > 0)
	{
		loopMode = MIDIVorbisRenderer::LoopMode::Short;
	}
	
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

	MIDIVorbisRenderer renderer(loopMode, beatDivision);
	try
	{
		renderer.loadSoundfont(soundfontPath);
	}
	catch (std::exception& e)
	{
		std::cout << "Failed to load soundfont at " << soundfontPath << ": " <<
			e.what() << std::endl;
		return 1;
	}

	for (int i = 0; i < midiFiles.size(); i++)
	{
		try
		{
			std::cout << "Rendering " << midiFiles[i] << std::endl;
			renderer.renderFile(midiFiles[i], outputFiles[i]);
			std::cout << "Output: " << outputFiles[i] << std::endl;
		}
		catch (std::exception& e)
		{
			std::cout << "Failed to create render for file " << midiFiles[i] << ": " <<
				e.what() << std::endl;
		}
	}

    return 0;
}