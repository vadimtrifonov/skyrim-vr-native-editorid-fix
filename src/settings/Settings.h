#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace Settings
{
	struct Values
	{
		bool enableLookupTable = true;
		bool enableNativeEditorIDLookup = true;
		std::size_t maxEditorIDLength = 128;

		bool boundsCheckNativeFormatting = true;
		bool excludeEditorIDFromNodeNaming = true;
		bool helpCommandSwitchNamePriority = true;

		bool traceSetFormEditorID = false;
		bool traceNativeEditorIDMap = false;
		bool traceGetFormEditorID = false;
		bool traceGetReferenceEditorID = false;
	};

	struct LoadResult
	{
		Values settings;
		std::vector<std::string> warnings;
	};

	[[nodiscard]] std::filesystem::path GetDefaultPath();
	[[nodiscard]] LoadResult LoadWithDiagnostics(const std::filesystem::path& path);
	[[nodiscard]] Values Load(const std::filesystem::path& path);
}
