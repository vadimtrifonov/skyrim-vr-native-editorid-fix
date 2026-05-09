#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace LookupTable
{
	using EditorID = const std::string*;

	struct Options
	{
		bool enabled = true;
		std::size_t maxEditorIDLength = 128;
	};

	enum class SetEditorIDStatus
	{
		Ignored,
		Unchanged,
		Changed
	};

	struct SetEditorIDResult
	{
		SetEditorIDStatus status = SetEditorIDStatus::Ignored;
		EditorID previousEditorID;
		EditorID currentEditorID;
	};

	void Reset(Options options);

	[[nodiscard]] EditorID FindEditorID(std::uint32_t formID, std::uint8_t formType);
	[[nodiscard]] SetEditorIDResult SetEditorID(std::uint32_t formID, std::uint8_t formType, const char* editorID);
}
