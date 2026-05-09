#pragma once

#include <cstddef>

namespace Hooks::NodeNaming
{
	struct InstallResult
	{
		bool active = false;
		std::size_t validatedSites = 0;
		std::size_t requiredSites = 0;
	};

	[[nodiscard]] InstallResult Install();
}
