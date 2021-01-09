#include "pathresolution.h"
#include <vector>
#include <sstream>
#include <filesystem>

using namespace std;
namespace fs = std::filesystem;

namespace midirenderer::utils
{
	void enumerateWildcardPath(const fs::path &parentFolder, size_t pathIndex, const vector<string> &pathList,
		function<void(string)> pathCallback);

	void resolveWildcardedPath(const std::string& path, std::function<void(std::string)> pathCallback)
	{
		fs::path fsPath = fs::u8path(path).lexically_normal();

		vector<string> pathComponents;

		fs::path root = fsPath.root_path();
		if (root.empty()) { root = fs::u8path("."); }

		for (const auto& pathComponent : fsPath.relative_path())
		{
			pathComponents.push_back(pathComponent.u8string());
		}

#if _WINDOWS
		// UNC paths for SMB shares need the hostname and share name together
		// in order to be considered a valid path
		if (root.lexically_normal().string().at(0) == '\\')
		{
			root /= fs::u8path(pathComponents[1]);
			pathComponents.erase(pathComponents.begin());
		}
#endif
		error_code ec;
		if (fs::is_directory(root, ec))
		{
			enumerateWildcardPath(root, 0, pathComponents, pathCallback);
		}
	}

	void enumerateWildcardPath(const fs::path& parentFolder, size_t pathIndex, const vector<string>& pathList, function<void(string)> pathCallback)
	{
		string pathName = pathList.at(pathIndex);

		// Stop if the final path's name is empty - the folder _is_ the match
		// This happens when the original search string ends with a directory separator
		if (pathName.empty())
		{
			pathCallback(parentFolder.string());
		}

		bool isFinalPath = pathIndex == pathList.size() - 1;

		if (pathName.find('*') == string::npos)
		{
			// No wildcards
			fs::path nextPath = parentFolder / fs::u8path(pathName);
			if (fs::exists(nextPath))
			{
				if (isFinalPath)
				{
					pathCallback(nextPath.u8string());
				}
				else if (fs::is_directory(nextPath))
				{
					enumerateWildcardPath(nextPath, pathIndex + 1, pathList, pathCallback);
				}
			}
			return;
		}

		// Resolve wildcards
		stringstream pathNameStream = std::stringstream(pathName);
		string nameFragment;
		vector<string> nameFragments;
		while (getline(pathNameStream, nameFragment, '*'))
		{
			nameFragments.push_back(nameFragment);
		}

		for (const auto& entry : fs::directory_iterator(parentFolder))
		{
			// Skip files when still following directories
			// and all other non-file non-directory entries
			if (entry.is_regular_file())
			{
				if (!isFinalPath) { continue; }
			}
			else if (!entry.is_directory()) { continue; }

			string filename = entry.path().filename().u8string();

			size_t searchIndex = 0;
			for (const auto& fragment : nameFragments)
			{
				searchIndex = filename.find(fragment, searchIndex);
				if (searchIndex == string::npos)
				{
					break;
				}
				searchIndex += fragment.length();
			}
			if (searchIndex == filename.length() ||
				(searchIndex != -1 && pathName.back() == '*'))
			{
				fs::path foundPath = parentFolder / fs::u8path(filename);
				if (isFinalPath)
				{
					pathCallback(foundPath.u8string());
				}
				else
				{
					enumerateWildcardPath(foundPath, pathIndex + 1, pathList, pathCallback);
				}
			}
		}
	}
}
