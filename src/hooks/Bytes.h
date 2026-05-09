#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace REL
{
	class Segment;
}

namespace Hooks::Bytes
{
	struct AddressWindow
	{
		std::uintptr_t address = 0;
		std::size_t size = 0;
	};

	[[nodiscard]] std::optional<std::uintptr_t> CheckedAdd(std::uintptr_t value, std::size_t delta);
	[[nodiscard]] std::optional<std::uintptr_t> CheckedSub(std::uintptr_t value, std::size_t delta);
	[[nodiscard]] const void* AddressAsPointer(std::uintptr_t address) noexcept;
	[[nodiscard]] bool IsReadableWindow(const REL::Segment& segment, AddressWindow window);
	[[nodiscard]] std::optional<std::vector<std::uint8_t>> TryReadBytes(const REL::Segment& segment, AddressWindow window);
	[[nodiscard]] std::uintptr_t SegmentEnd(const REL::Segment& segment);
	[[nodiscard]] bool BytesMatch(std::span<const std::uint8_t> expected, std::span<const std::uint8_t> actual);
	[[nodiscard]] std::string FormatBytes(std::span<const std::uint8_t> bytes);
}
