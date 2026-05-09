#pragma once

#include <cstddef>

namespace Hooks::NativeFormatting
{
	[[nodiscard]] std::size_t GetTrampolineSize();
	void Install();
}
