#include "hooks/HelpCommandHooks.h"

#include "hooks/Bytes.h"
#include "pch.h"

#include "RE/C/CommandTable.h"

#include <array>
#include <bit>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

namespace
{
	constexpr REL::RelocationID kHelpCommandNameRetrievalID{ 22364, 22839 };
	constexpr REL::VariantOffset kHelpCommandNameRetrievalOffset{ 0x3E3, 0x823, 0x3E3 };
	constexpr std::size_t kCheckAndSkipBlockSize = 10;
	constexpr std::size_t kCheckAndSkipRel8Offset = 9;
	constexpr std::int8_t kCheckAndSkipRel8Adjustment = 4;
	constexpr std::size_t kFullNameRel32Offset = 4;
	constexpr std::size_t kHelpCommandVRPatchOffset = 0x3E3;
	constexpr std::size_t kHelpCommandVRPatchSize = 30;
	constexpr std::string_view kHelpCommandName = "Help";
	// Validated expected byte window for Skyrim VR 1.4.15.0 at the help command patch site.
	constexpr std::array<std::uint8_t, kHelpCommandVRPatchSize> kExpectedOriginalVR{
		0x48, 0x8B, 0x07,
		0x48, 0x8B, 0xCF,
		0xFF, 0x90, 0x90, 0x01, 0x00, 0x00,
		0x48, 0x85, 0xC0,
		0x74, 0x05,
		0x80, 0x38, 0x00,
		0x75, 0x08,
		0x48, 0x8B, 0xCF,
		0xE8, 0xDF, 0x37, 0xE8, 0xFF
	};

	using Hooks::Bytes::AddressWindow;
	using Hooks::Bytes::BytesMatch;
	using Hooks::Bytes::CheckedAdd;
	using Hooks::Bytes::FormatBytes;
	using Hooks::Bytes::IsReadableWindow;
	using Hooks::Bytes::SegmentEnd;
	using Hooks::Bytes::TryReadBytes;

	template <std::size_t N>
	[[nodiscard]] constexpr std::span<const std::uint8_t> AsByteSpan(const std::array<std::uint8_t, N>& bytes)
	{
		return std::span<const std::uint8_t>{ bytes.data(), bytes.size() };
	}

	struct HelpCommandPatchLayout
	{
		std::size_t getEditorIDBlockSize = 0;
		std::size_t fullNameBlockSize = 0;
		std::size_t fullNameBlockOffset = 0;
		std::int32_t fullNameRel32Adjustment = 0;
		std::size_t totalPatchSize = 0;
		std::string_view runtimeLabel;
		std::span<const std::uint8_t> expectedOriginal;
	};

	struct PreparedHelpCommandPatch
	{
		std::uintptr_t target = 0;
		std::uintptr_t executeHandler = 0;
		std::vector<std::uint8_t> originalBytes;
		std::vector<std::uint8_t> patchedBytes;
		std::string_view runtimeLabel;
	};

	struct Relative8Adjustment
	{
		std::size_t offset = 0;
		std::int8_t delta = 0;
	};

	struct Relative32Adjustment
	{
		std::size_t offset = 0;
		std::int32_t delta = 0;
	};

	[[nodiscard]] std::optional<HelpCommandPatchLayout> GetPatchLayout()
	{
		if (REL::Module::IsAE()) {
			return HelpCommandPatchLayout{
				20,
				16,
				30,
				30,
				46,
				"Skyrim AE",
				{}
			};
		}

		if (!REL::Module::IsVR()) {
			return HelpCommandPatchLayout{
				12,
				8,
				22,
				22,
				30,
				"Skyrim SE",
				{}
			};
		}

		return HelpCommandPatchLayout{
			12,
			8,
			22,
			22,
			kExpectedOriginalVR.size(),
			"Skyrim VR",
			AsByteSpan(kExpectedOriginalVR)
		};
	}

	[[nodiscard]] std::optional<std::pair<std::uintptr_t, std::uintptr_t>> ResolvePatchSite(const HelpCommandPatchLayout& layout)
	{
		if (!REL::Module::IsVR()) {
			REL::Relocation<std::uintptr_t> target{ kHelpCommandNameRetrievalID, kHelpCommandNameRetrievalOffset };
			if (target.address() == 0) {
				logs::warn(
					"Skipping help command full-name priority patch because the {} target could not be resolved from the address library",
					layout.runtimeLabel);
				return std::nullopt;
			}

			return std::pair<std::uintptr_t, std::uintptr_t>{ target.address(), 0 };
		}

		auto* const helpCommand = RE::SCRIPT_FUNCTION::LocateConsoleCommand(kHelpCommandName);
		if (helpCommand == nullptr) {
			logs::warn("Skipping help command full-name priority patch because the '{}' console command could not be located", kHelpCommandName);
			return std::nullopt;
		}
		if (helpCommand->executeFunction == nullptr) {
			logs::warn("Skipping help command full-name priority patch because the '{}' console command has no execute handler", kHelpCommandName);
			return std::nullopt;
		}

		const auto executeHandler = SKSE::stl::unrestricted_cast<std::uintptr_t>(helpCommand->executeFunction);
		const auto target = CheckedAdd(executeHandler, kHelpCommandVRPatchOffset);
		if (!target) {
			logs::warn(
				"Skipping help command full-name priority patch because the derived {} patch site overflowed. execute=0x{:X} offset=0x{:X}",
				layout.runtimeLabel,
				executeHandler,
				kHelpCommandVRPatchOffset);
			return std::nullopt;
		}

		return std::pair<std::uintptr_t, std::uintptr_t>{ *target, executeHandler };
	}

	void AdjustRelative8(std::vector<std::uint8_t>& bytes, Relative8Adjustment adjustment)
	{
		if (adjustment.offset >= bytes.size()) {
			throw std::runtime_error("Help command patch rel8 offset exceeded the copied instruction block.");
		}

		const auto originalByte = static_cast<unsigned char>(bytes[adjustment.offset]);
		const auto original = static_cast<std::int16_t>(static_cast<std::int8_t>(originalByte));
		const auto adjusted = original + adjustment.delta;
		if (adjusted < INT8_MIN || adjusted > INT8_MAX) {
			throw std::runtime_error("Help command patch rel8 adjustment overflowed.");
		}

		bytes[adjustment.offset] = std::bit_cast<std::uint8_t>(static_cast<std::int8_t>(adjusted));
	}

	void AdjustRelative32(std::vector<std::uint8_t>& bytes, Relative32Adjustment adjustment)
	{
		if ((bytes.size() - adjustment.offset) < sizeof(std::int32_t)) {
			throw std::runtime_error("Help command patch rel32 offset exceeded the copied instruction block.");
		}

		std::int32_t original = 0;
		std::memcpy(&original, bytes.data() + adjustment.offset, sizeof(original));

		const auto adjusted = static_cast<std::int64_t>(original) + static_cast<std::int64_t>(adjustment.delta);
		if (adjusted < INT32_MIN || adjusted > INT32_MAX) {
			throw std::runtime_error("Help command patch rel32 adjustment overflowed.");
		}

		original = static_cast<std::int32_t>(adjusted);
		std::memcpy(bytes.data() + adjustment.offset, &original, sizeof(original));
	}

	[[nodiscard]] std::optional<PreparedHelpCommandPatch> PrepareHelpCommandPatch()
	{
		const auto layout = GetPatchLayout();
		if (!layout) {
			return std::nullopt;
		}

		const auto resolvedSite = ResolvePatchSite(*layout);
		if (!resolvedSite) {
			return std::nullopt;
		}

		const auto [target, executeHandler] = *resolvedSite;
		const auto textSegment = REL::Module::get().segment(REL::Segment::textx);
		if (!IsReadableWindow(textSegment, { target, layout->totalPatchSize })) {
			logs::warn(
				"Skipping help command full-name priority patch because the {} patch window was unreadable. target=0x{:X} size={} text=[0x{:X},0x{:X})",
				layout->runtimeLabel,
				target,
				layout->totalPatchSize,
				textSegment.address(),
				SegmentEnd(textSegment));
			return std::nullopt;
		}

		const auto originalBytes = TryReadBytes(textSegment, { target, layout->totalPatchSize });
		if (!originalBytes) {
			logs::warn(
				"Skipping help command full-name priority patch because the {} original byte window could not be read. target=0x{:X} size={}",
				layout->runtimeLabel,
				target,
				layout->totalPatchSize);
			return std::nullopt;
		}
		if (!layout->expectedOriginal.empty() && !BytesMatch(layout->expectedOriginal, *originalBytes)) {
			logs::warn(
				"Skipping help command full-name priority patch because the {} original byte window did not match. execute=0x{:X} target=0x{:X} size={} expected=[{}] actual=[{}]",
				layout->runtimeLabel,
				executeHandler,
				target,
				layout->totalPatchSize,
				FormatBytes(layout->expectedOriginal),
				FormatBytes(*originalBytes));
			return std::nullopt;
		}

		PreparedHelpCommandPatch prepared{};
		prepared.target = target;
		prepared.executeHandler = executeHandler;
		prepared.runtimeLabel = layout->runtimeLabel;
		prepared.originalBytes = *originalBytes;

		std::vector<std::uint8_t> getEditorIDBytes(layout->getEditorIDBlockSize);
		std::vector<std::uint8_t> checkAndSkipBytes(kCheckAndSkipBlockSize);
		std::vector<std::uint8_t> getFullNameBytes(layout->fullNameBlockSize);

		std::memcpy(getEditorIDBytes.data(), prepared.originalBytes.data(), getEditorIDBytes.size());
		std::memcpy(checkAndSkipBytes.data(), prepared.originalBytes.data() + getEditorIDBytes.size(), checkAndSkipBytes.size());
		std::memcpy(getFullNameBytes.data(), prepared.originalBytes.data() + layout->fullNameBlockOffset, getFullNameBytes.size());

		AdjustRelative8(checkAndSkipBytes, { kCheckAndSkipRel8Offset, kCheckAndSkipRel8Adjustment });
		AdjustRelative32(getFullNameBytes, { kFullNameRel32Offset, layout->fullNameRel32Adjustment });

		prepared.patchedBytes.resize(layout->totalPatchSize);
		std::memcpy(prepared.patchedBytes.data(), getFullNameBytes.data(), getFullNameBytes.size());
		std::memcpy(prepared.patchedBytes.data() + getFullNameBytes.size(), checkAndSkipBytes.data(), checkAndSkipBytes.size());
		std::memcpy(
			prepared.patchedBytes.data() + getFullNameBytes.size() + checkAndSkipBytes.size(),
			getEditorIDBytes.data(),
			getEditorIDBytes.size());

		return prepared;
	}
}

namespace Hooks::HelpCommand
{
	void Install()
	{
		static bool installed = false;
		if (installed) {
			return;
		}

		const auto prepared = PrepareHelpCommandPatch();
		if (!prepared) {
			return;
		}

		REL::safe_write(prepared->target, prepared->patchedBytes.data(), prepared->patchedBytes.size());
		logs::info(
			"Installed help command full-name priority patch for {} at 0x{:X} (execute=0x{:X}, {} bytes)",
			prepared->runtimeLabel,
			prepared->target,
			prepared->executeHandler,
			prepared->patchedBytes.size());

		installed = true;
	}
}
