#pragma once

#include <string>
#include <functional>

namespace midirenderer::utils
{
	void resolveWildcardedPath(const std::string& path, std::function<void(std::string)> pathCallback);
}
