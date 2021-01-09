#include "platformargswrapper.h"

#include "platformsupport.h"

PlatformArgsWrapper::PlatformArgsWrapper(int argc, argv_t** argv) : m_argc(argc)
{
	for (int i = 0; i < argc; i++)
	{
		consumeArg(argv[i]);
	}
}

void PlatformArgsWrapper::consumeArg(argv_t* arg)
{
#if _WIN32
	int multibyteSize = WideCharToMultiByte(CP_UTF8, 0, arg, -1, NULL, 0, NULL, NULL);
	auto utf8Copy = std::make_unique<char[]>(multibyteSize);
	WideCharToMultiByte(CP_UTF8, 0, arg, -1, utf8Copy.get(), multibyteSize, NULL, NULL);
	m_argv.push_back(utf8Copy.get());
	m_managedArgs.push_back(std::move(utf8Copy));
#else
	m_argv.push_back(arg);
#endif
}