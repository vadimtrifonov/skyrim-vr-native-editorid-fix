#include "pch.h"

#include <Windows.h>

#include "settings/Settings.h"

namespace
{
	template <class T>
	struct ParseResult
	{
		T value;
		bool malformed = false;
	};

	struct IniEntry
	{
		std::wstring_view section;
		std::wstring_view key;
	};

	std::wstring ReadRawValue(const std::filesystem::path& path, IniEntry entry)
	{
		const std::wstring sectionName(entry.section);
		const std::wstring keyName(entry.key);
		std::array<wchar_t, 64> buffer{};
		const auto length = GetPrivateProfileStringW(
			sectionName.c_str(),
			keyName.c_str(),
			L"",
			buffer.data(),
			static_cast<DWORD>(buffer.size()),
			path.c_str());

		return std::wstring(buffer.data(), length);
	}

	std::wstring Trim(std::wstring value)
	{
		const auto notSpace = [](wchar_t ch) {
			return !std::iswspace(ch);
		};

		const auto begin = std::find_if(value.begin(), value.end(), notSpace);
		const auto end = std::find_if(value.rbegin(), value.rend(), notSpace).base();
		if (begin >= end) {
			return L"";
		}

		return std::wstring(begin, end);
	}

	std::wstring Lower(std::wstring value)
	{
		std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
			return static_cast<wchar_t>(std::towlower(ch));
		});
		return value;
	}

	std::string ToUtf8(std::wstring_view value)
	{
		return SKSE::stl::utf16_to_utf8(value).value_or(std::string("<unicode conversion error>"));
	}

	void AddMalformedWarning(
		std::vector<std::string>& warnings,
		IniEntry entry,
		const std::wstring& raw,
		std::string_view fallback)
	{
		warnings.push_back(
			"Invalid INI value [" + ToUtf8(entry.section) +
			"] " + ToUtf8(entry.key) +
			"='" + ToUtf8(raw) +
			"'; using fallback " + std::string(fallback));
	}

	ParseResult<bool> ParseBool(const std::wstring& raw, bool fallback)
	{
		if (raw.empty()) {
			return { fallback, false };
		}

		const auto value = Lower(Trim(raw));
		if (value == L"1" || value == L"true" || value == L"yes" || value == L"on") {
			return { true, false };
		}
		if (value == L"0" || value == L"false" || value == L"no" || value == L"off") {
			return { false, false };
		}

		return { fallback, true };
	}

	ParseResult<std::size_t> ParseSizeT(const std::wstring& raw, std::size_t fallback)
	{
		if (raw.empty()) {
			return { fallback, false };
		}

		const auto value = Trim(raw);
		std::size_t index = 0;

		try {
			const auto parsed = std::stoull(value, &index, 10);
			return index == value.size() ?
			           ParseResult<std::size_t>{ static_cast<std::size_t>(parsed), false } :
			           ParseResult<std::size_t>{ fallback, true };
		} catch (...) {
			return { fallback, true };
		}
	}
}

namespace Settings
{
	std::filesystem::path GetDefaultPath()
	{
		return L"Data/SKSE/Plugins/NativeEditorIDFix.ini";
	}

	LoadResult LoadWithDiagnostics(const std::filesystem::path& path)
	{
		LoadResult result{};

		auto applyBool = [&](IniEntry entry, bool Values::* member) {
			const auto raw = ReadRawValue(path, entry);
			const auto parsed = ParseBool(raw, result.settings.*member);
			if (parsed.malformed) {
				AddMalformedWarning(result.warnings, entry, raw, parsed.value ? "true" : "false");
			}
			result.settings.*member = parsed.value;
		};

		auto applySize = [&](IniEntry entry, std::size_t Values::* member) {
			const auto raw = ReadRawValue(path, entry);
			const auto parsed = ParseSizeT(raw, result.settings.*member);
			if (parsed.malformed) {
				AddMalformedWarning(result.warnings, entry, raw, std::to_string(parsed.value));
			}
			result.settings.*member = parsed.value;
		};

		applyBool({ L"Main", L"EnableLookupTable" }, &Values::enableLookupTable);
		applyBool({ L"Main", L"EnableNativeEditorIDLookup" }, &Values::enableNativeEditorIDLookup);
		applySize({ L"Main", L"MaxEditorIDLength" }, &Values::maxEditorIDLength);
		applyBool({ L"Patches", L"BoundsCheckNativeFormatting" }, &Values::boundsCheckNativeFormatting);
		applyBool({ L"Patches", L"ExcludeEditorIDFromNodeNaming" }, &Values::excludeEditorIDFromNodeNaming);
		applyBool({ L"Patches", L"HelpCommandSwitchNamePriority" }, &Values::helpCommandSwitchNamePriority);
		applyBool({ L"Debug", L"TraceSetFormEditorID" }, &Values::traceSetFormEditorID);
		applyBool({ L"Debug", L"TraceNativeEditorIDMap" }, &Values::traceNativeEditorIDMap);
		applyBool({ L"Debug", L"TraceGetFormEditorID" }, &Values::traceGetFormEditorID);
		applyBool({ L"Debug", L"TraceGetReferenceEditorID" }, &Values::traceGetReferenceEditorID);

		return result;
	}

	Values Load(const std::filesystem::path& path)
	{
		return LoadWithDiagnostics(path).settings;
	}
}
