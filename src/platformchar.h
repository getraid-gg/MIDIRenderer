#pragma once
#include <string>
#include <memory>

#if _WIN32
typedef wchar_t argv_t;
namespace midirenderer
{
	typedef wchar_t platformchar_t;
}
#define MAIN wmain

#else
typedef char argv_t;
namespace midirenderer
{
	typedef char platformchar_t;
}
#define MAIN main
#endif