#pragma once

#include <string>
#include "platformchar.h"

#if _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

/*
 * Despite the documentation stating otherwise, FluidSynth does not support UTF - 16 filenames
 * under Windows. Non-ANSI UTF-8 filenames are not converted to UTF-16 and passed through _wfopen()
 * and as a result, Windows users of libfluidsynth cannot load files through the library by name if
 * non-ANSI filename support is desired. I've submitted a PR that has been merged, but there's no
 * telling when it will be included in a release; my conservative guess is that it will be in the
 * next major revision - FluidSynth is currently version 2.x.x so 3.x.x is the estimate.
 * In the meantime, a workaround is in place to allow as much UTF-8/UTF-16 support as possible
 * through FluidSynth on Windows, given the interface for loading files.
*/
#ifndef FLUIDSYNTH_API
#define FLUIDSYNTH_API __declspec(dllimport)
#endif
#include <fluidsynth/version.h>

#if !defined(WINDOWS_UTF16_WORKAROUND) && (FLUID_VERSION_MAJOR < 3)
#define WINDOWS_UTF16_WORKAROUND
#endif

#endif

namespace midirenderer
{
	namespace stringutils
	{
		std::basic_string<platformchar_t> getPlatformString(std::string source);
	}
}