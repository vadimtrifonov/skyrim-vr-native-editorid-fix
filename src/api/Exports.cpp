#include "pch.h"

#include "api/NativeEditorIDFixAPI.hpp"
#include "lookup/LookupMode.h"
#include "lookup/LookupTable.h"

namespace
{
	using LookupStateFn = NEIF::Details::GetEditorIDLookupStateT;
	using GetEditorIDFn = NEIF::Details::GetEditorIDT;

	static_assert(static_cast<std::uint32_t>(LookupMode::Value::None) == static_cast<std::uint32_t>(NEIF::EditorIDLookupState::None));
	static_assert(static_cast<std::uint32_t>(LookupMode::Value::ExternalOnly) == static_cast<std::uint32_t>(NEIF::EditorIDLookupState::ExternalOnly));
	static_assert(static_cast<std::uint32_t>(LookupMode::Value::NativeNoNodes) == static_cast<std::uint32_t>(NEIF::EditorIDLookupState::NativeNoNodes));
	static_assert(static_cast<std::uint32_t>(LookupMode::Value::Native) == static_cast<std::uint32_t>(NEIF::EditorIDLookupState::Native));
}

extern "C" __declspec(dllexport) NEIF::EditorIDLookupState NEIF_GetEditorIDLookupState()
{
	static_assert(std::is_same_v<decltype(&NEIF_GetEditorIDLookupState), LookupStateFn>);
	static_assert(std::string_view("NEIF_GetEditorIDLookupState") == NEIF::Details::GetEditorIDLookupStateName);

	return static_cast<NEIF::EditorIDLookupState>(LookupMode::GetEffective());
}

extern "C" __declspec(dllexport) const char* NEIF_GetEditorID(NEIF::FormInfo form)
{
	static_assert(std::is_same_v<decltype(&NEIF_GetEditorID), GetEditorIDFn>);
	static_assert(std::string_view("NEIF_GetEditorID") == NEIF::Details::GetEditorIDName);

	const auto* editorID = LookupTable::FindEditorID(form.ID, form.Type);
	return editorID != nullptr ? editorID->c_str() : "";
}
