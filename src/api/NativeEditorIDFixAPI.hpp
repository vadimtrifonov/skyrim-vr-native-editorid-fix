//----------------------------------------------------------------------------------------------
// Native EditorID Fix
// Copyright (c) 2023 Kitsuune - Apache License 2.0
//

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN 1
#endif

#ifndef NOMINMAX
#	define NOMINMAX 1
#endif

#include <Windows.h>
#include <cstdint>

namespace NEIF  // Native Editor ID Fix
{
	enum class EditorIDLookupState : uint32_t
	{
		None,
		ExternalOnly,
		NativeNoNodes,
		Native
	};

	struct TESForm
	{
		uint8_t Pad0[0x14];
		uint32_t FormID;
		uint8_t Pad18[0x2];
		uint8_t FormType;
		uint8_t Pad1B[0x3];
	};

	struct FormInfo
	{
		FormInfo() = default;
		FormInfo(uint32_t id, uint8_t type) :
			ID(id), Type(type)
		{}
		FormInfo(const TESForm& in) :
			ID(in.FormID), Type(in.FormType)
		{}

		uint32_t ID;
		uint8_t Type;
	};

	namespace Details
	{
		template <class T>
		auto ExplicitImport(const char* name)
		{
			auto handle = GetModuleHandleW(L"NativeEditorIDFix");
			return handle ? reinterpret_cast<T>(GetProcAddress(handle, name)) : nullptr;
		}

		using GetEditorIDLookupStateT = EditorIDLookupState (*)();
		using GetEditorIDT = const char* (*)(FormInfo);

		constexpr const char* GetEditorIDLookupStateName = "NEIF_GetEditorIDLookupState";
		constexpr const char* GetEditorIDName = "NEIF_GetEditorID";
	}

	inline EditorIDLookupState GetEditorIDLookupState()
	{
		static Details::GetEditorIDLookupStateT func = nullptr;
		if (!func) {
			func = Details::ExplicitImport<Details::GetEditorIDLookupStateT>(Details::GetEditorIDLookupStateName);
		}
		return func ? func() : EditorIDLookupState::None;
	}

	inline const char* GetEditorID(const void* form)
	{
		static Details::GetEditorIDT func = nullptr;
		if (!func) {
			func = Details::ExplicitImport<Details::GetEditorIDT>(Details::GetEditorIDName);
		}
		return form && func ? func(*static_cast<const TESForm*>(form)) : "";
	}
}
