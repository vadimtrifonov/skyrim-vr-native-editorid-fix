#include "hooks/Bytes.h"

#include "pch.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>

namespace Hooks::Bytes
{
	std::optional<std::uintptr_t> CheckedAdd(std::uintptr_t value, std::size_t delta)
	{
		if (delta > (std::numeric_limits<std::uintptr_t>::max)() - value) {
			return std::nullopt;
		}
		return value + delta;
	}

	std::optional<std::uintptr_t> CheckedSub(std::uintptr_t value, std::size_t delta)
	{
		if (delta > value) {
			return std::nullopt;
		}
		return value - delta;
	}

	const void* AddressAsPointer(std::uintptr_t address) noexcept
	{
		return reinterpret_cast<const void*>(address);  // NOLINT(performance-no-int-to-ptr)
	}

	bool IsReadableWindow(const REL::Segment& segment, AddressWindow window)
	{
		if (segment.address() == 0 || segment.size() == 0 || window.address < segment.address()) {
			return false;
		}

		const auto offset = window.address - segment.address();
		if (offset > segment.size()) {
			return false;
		}

		return window.size <= (segment.size() - offset);
	}

	std::optional<std::vector<std::uint8_t>> TryReadBytes(const REL::Segment& segment, AddressWindow window)
	{
		if (!IsReadableWindow(segment, window)) {
			return std::nullopt;
		}

		std::vector<std::uint8_t> bytes(window.size);
		std::memcpy(bytes.data(), AddressAsPointer(window.address), bytes.size());
		return bytes;
	}

	std::uintptr_t SegmentEnd(const REL::Segment& segment)
	{
		return CheckedAdd(segment.address(), segment.size()).value_or(segment.address());
	}

	bool BytesMatch(std::span<const std::uint8_t> expected, std::span<const std::uint8_t> actual)
	{
		return expected.size() == actual.size() && std::equal(expected.begin(), expected.end(), actual.begin());
	}

	std::string FormatBytes(std::span<const std::uint8_t> bytes)
	{
		std::ostringstream stream;
		stream << std::hex << std::uppercase << std::setfill('0');
		for (std::size_t index = 0; index < bytes.size(); ++index) {
			if (index != 0) {
				stream << ' ';
			}
			stream << std::setw(2) << static_cast<unsigned>(bytes[index]);
		}
		return stream.str();
	}
}
