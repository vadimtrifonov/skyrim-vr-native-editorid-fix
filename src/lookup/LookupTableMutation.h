#pragma once

#include "lookup/LookupTable.h"

#include <functional>

namespace LookupTable::Mutation
{
	using EditorIDPublisher = std::function<void(EditorID previousEditorID, EditorID currentEditorID)>;

	[[nodiscard]] SetEditorIDResult SetEditorID(
		std::uint32_t formID,
		std::uint8_t formType,
		const char* editorID,
		const EditorIDPublisher& publisher);
}
