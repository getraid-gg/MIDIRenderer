#pragma once
#include "platformchar.h"

#include <vector>
#include <memory>

class PlatformArgsWrapper
{
public:
	PlatformArgsWrapper(int argc, argv_t** argv);

	int getArgc() const { return m_argc; }
	char** getArgv() { return m_argv.data(); }
private:
	void consumeArg(argv_t* arg);

	int m_argc;
	std::vector<char*> m_argv;
	std::vector<std::unique_ptr<char[]>> m_managedArgs;
};