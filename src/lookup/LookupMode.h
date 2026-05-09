#pragma once

#include "settings/Settings.h"

#include <cstdint>

namespace LookupMode
{
	enum class Value : std::uint32_t
	{
		None = 0,
		ExternalOnly = 1,
		NativeNoNodes = 2,
		Native = 3
	};

	[[nodiscard]] Value FromSettings(const Settings::Values& settings);
	[[nodiscard]] Value ResolveEffective(Value configuredMode, bool nativeGettersActive, bool nodeNamingCompatibilityActive);

	void SetEffective(Value mode);
	[[nodiscard]] Value GetEffective();
}
