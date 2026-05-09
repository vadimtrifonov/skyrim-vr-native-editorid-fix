#pragma once

#include "lookup/LookupMode.h"
#include "settings/Settings.h"

#include <cstddef>
#include <cstdint>

namespace Hooks::Lookup
{
	struct InstallResult
	{
		bool setterHookActive = false;
		bool nativeGettersActive = false;
	};

	[[nodiscard]] std::size_t GetTrampolineSize(LookupMode::Value mode);
	[[nodiscard]] InstallResult Install(LookupMode::Value mode, const Settings::Values& settings);
}
