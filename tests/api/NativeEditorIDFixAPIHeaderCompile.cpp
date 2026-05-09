#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN 1
#endif

#ifndef NOMINMAX
#	define NOMINMAX 1
#endif

#include <Windows.h>

#include "api/NativeEditorIDFixAPI.hpp"

int NativeEditorIDFixAPIHeaderCompileAnchor()
{
	[[maybe_unused]] const auto state = NEIF::EditorIDLookupState::None;
	[[maybe_unused]] const auto form = NEIF::FormInfo(0x1234u, 0x01u);
	return 0;
}
