#include "platformsupport.h"

namespace midirenderer
{
	namespace stringutils
	{
		std::basic_string<platformchar_t> getPlatformString(std::string source)
		{
#if _WIN32
			int utf16Size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source.c_str(), -1, NULL, 0);
			auto utf16CString = std::make_unique<wchar_t[]>(utf16Size);
			MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source.c_str(), -1, utf16CString.get(), utf16Size);
			return std::basic_string<platformchar_t>(utf16CString.get());
#else
			return source;
#endif
		}
	}
}
