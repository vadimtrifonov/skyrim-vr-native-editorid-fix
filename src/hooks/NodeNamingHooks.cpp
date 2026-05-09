#include "hooks/NodeNamingHooks.h"

#include "hooks/Bytes.h"
#include "pch.h"
#include "runtime/RuntimeInfo.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <vector>

namespace
{
	constexpr std::size_t kNodeNamingPatchSize = 9;
	constexpr std::array<std::uint8_t, kNodeNamingPatchSize> kCallViaRax190{
		0x48, 0x8B, 0x01, 0xFF, 0x90, 0x90, 0x01, 0x00, 0x00
	};
	constexpr std::array<std::uint8_t, kNodeNamingPatchSize> kCallViaRdx190FromRsi{
		0x48, 0x8B, 0xCE, 0xFF, 0x92, 0x90, 0x01, 0x00, 0x00
	};
	constexpr std::size_t kTESObjectNodeNamingAVR = 0x1D94A0;
	constexpr std::size_t kTESObjectNodeNamingBVR = 0x1D9810;
	constexpr std::size_t kTESObjectNodeNamingCVR = 0x1DA590;
	constexpr std::size_t kEmptyStringVR = 0x15BCC7C;
	// Validated expected byte windows for Skyrim VR 1.4.15.0 node-naming patch sites.
	constexpr std::array<std::uint8_t, 15> kValidationPrefix1D7B6B{
		0x0F, 0x28, 0xC7, 0xF3, 0x41, 0x0F, 0x59, 0xC5, 0x0F, 0x57, 0xF6, 0xF3, 0x0F, 0x5A, 0xF0
	};
	constexpr std::array<std::uint8_t, 16> kValidationSuffix1D7B6B{
		0x48, 0x8B, 0xF8, 0x48, 0x8B, 0xCB, 0xE8, 0xE1, 0x8F, 0x18, 0x00, 0x8B, 0xD8, 0x48, 0x8B, 0x16
	};
	constexpr std::array<std::uint8_t, 13> kValidationPrefix1D7B84{
		0x48, 0x8B, 0xCB, 0xE8, 0xE1, 0x8F, 0x18, 0x00, 0x8B, 0xD8, 0x48, 0x8B, 0x16
	};
	constexpr std::array<std::uint8_t, 14> kValidationSuffix1D7B84{
		0x4C, 0x8B, 0xC8, 0xF2, 0x0F, 0x11, 0x74, 0x24, 0x40, 0x44, 0x89, 0x74, 0x24, 0x38
	};
	constexpr std::array<std::uint8_t, 15> kValidationPrefix1D8F40{
		0x48, 0x89, 0x45, 0xC7, 0x89, 0x45, 0xCF, 0x4B, 0x8B, 0x4C, 0x37, 0x10, 0x8B, 0x59, 0x14
	};
	constexpr std::array<std::uint8_t, 14> kValidationSuffix1D8F40{
		0x4C, 0x8B, 0xC8, 0x89, 0x5C, 0x24, 0x20, 0x4C, 0x8D, 0x05, 0x25, 0x3D, 0x3E, 0x01
	};
	constexpr std::array<std::uint8_t, 20> kValidationPrefix1D9605{
		0x4C, 0x89, 0x7C, 0x24, 0x40, 0x44, 0x89, 0x7C, 0x24, 0x48, 0x48, 0x8B, 0x8D, 0x48, 0x13, 0x00,
		0x00, 0x8B, 0x79, 0x14
	};
	constexpr std::array<std::uint8_t, 14> kValidationSuffix1D9605{
		0x4C, 0x8B, 0xC8, 0x89, 0x7C, 0x24, 0x20, 0x4C, 0x8D, 0x05, 0x7C, 0x36, 0x3E, 0x01
	};
	constexpr std::array<std::uint8_t, 20> kValidationPrefix1D9B3F{
		0x49, 0x8B, 0x4C, 0x2E, 0x10, 0x48, 0x85, 0xC9, 0x74, 0x61, 0x49, 0x83, 0x7D, 0x00, 0x00, 0x74,
		0x5A, 0x8B, 0x59, 0x14
	};
	constexpr std::array<std::uint8_t, 16> kValidationSuffix1D9B3F{
		0x89, 0x5C, 0x24, 0x28, 0x48, 0x89, 0x44, 0x24, 0x20, 0x4C, 0x8D, 0x0D, 0x10, 0x7E, 0x3D, 0x01
	};
	constexpr std::array<std::uint8_t, 17> kValidationPrefix1DA6B6{
		0x44, 0x89, 0x64, 0x24, 0x50, 0x4D, 0x6B, 0xEF, 0x78, 0x49, 0x8B, 0x4C, 0x35, 0x10, 0x8B, 0x79,
		0x14
	};
	constexpr std::array<std::uint8_t, 13> kValidationSuffix1DA6B6{
		0x4C, 0x8B, 0xC0, 0x44, 0x8B, 0xCF, 0x48, 0x8D, 0x15, 0x3C, 0x27, 0x3E, 0x01
	};

	using Hooks::Bytes::BytesMatch;
	using Hooks::Bytes::CheckedAdd;
	using Hooks::Bytes::CheckedSub;
	using Hooks::Bytes::FormatBytes;
	using Hooks::Bytes::SegmentEnd;
	using Hooks::Bytes::TryReadBytes;

	template <std::size_t N>
	[[nodiscard]] constexpr std::span<const std::uint8_t> AsByteSpan(const std::array<std::uint8_t, N>& bytes)
	{
		return std::span<const std::uint8_t>{ bytes.data(), bytes.size() };
	}

	struct NodeNamingPatchSite
	{
		REL::VariantID id;
		REL::VariantOffset offset;
		std::string_view label;
		std::string_view confidence;
		std::span<const std::uint8_t> expectedPrefix;
		std::array<std::uint8_t, kNodeNamingPatchSize> expectedOriginal;
		std::span<const std::uint8_t> expectedSuffix;
	};

	struct PlannedNodeNamingPatch
	{
		std::uintptr_t target = 0;
		std::array<std::uint8_t, kNodeNamingPatchSize> patch{};
		std::array<std::uint8_t, kNodeNamingPatchSize> original{};
		std::string_view label;
		std::string_view confidence;
	};

	struct NodeNamingPatchPlan
	{
		std::uintptr_t emptyString = 0;
		std::vector<PlannedNodeNamingPatch> patches;
		std::size_t requiredSiteCount = 0;
	};

	void LogUnreadableValidationWindow(
		std::string_view siteLabel,
		std::string_view windowLabel,
		std::uintptr_t address,
		std::size_t size,
		const REL::Segment& segment)
	{
		logs::warn(
			"Skipping {} because the {} validation window could not be read from the loaded text segment. address=0x{:X} size={} text=[0x{:X},0x{:X})",
			siteLabel,
			windowLabel,
			address,
			size,
			segment.address(),
			SegmentEnd(segment));
	}

	[[nodiscard]] std::array<NodeNamingPatchSite, 6> GetNodeNamingPatchSites()
	{
		return {
			// VR function offsets were derived from skyrim_vr_address_library/addrlib.csv.
			// VR local offsets below were validated against Skyrim VR 1.4.15.0.
			NodeNamingPatchSite{
				REL::VariantID(15501, 15678, 0x1D7450),
				REL::VariantOffset(0x71B, 0x7FD, 0x71B),
				"AttachArmorAddon node naming call A (15501 + 0x71B)",
				"medium",
				AsByteSpan(kValidationPrefix1D7B6B),
				kCallViaRax190,
				AsByteSpan(kValidationSuffix1D7B6B) },
			NodeNamingPatchSite{
				REL::VariantID(15501, 15678, 0x1D7450),
				REL::VariantOffset(0x734, 0x816, 0x734),
				"AttachArmorAddon node naming call B (15501 + 0x734)",
				"medium",
				AsByteSpan(kValidationPrefix1D7B84),
				kCallViaRdx190FromRsi,
				AsByteSpan(kValidationSuffix1D7B84) },
			NodeNamingPatchSite{
				REL::VariantID(15506, 15683, 0x1D8BE0),
				REL::VariantOffset(0x360, 0x35D, 0x360),
				"TESForm node naming call (15506 + 0x360)",
				"medium",
				AsByteSpan(kValidationPrefix1D8F40),
				kCallViaRax190,
				AsByteSpan(kValidationSuffix1D8F40) },
			NodeNamingPatchSite{
				REL::VariantID(15511, 15688, kTESObjectNodeNamingAVR),
				REL::VariantOffset(0x165, 0x1BD, 0x165),
				"TESObjectREFR node naming call A (15511 + 0x165)",
				"strong",
				AsByteSpan(kValidationPrefix1D9605),
				kCallViaRax190,
				AsByteSpan(kValidationSuffix1D9605) },
			NodeNamingPatchSite{
				REL::VariantID(15514, 15691, kTESObjectNodeNamingBVR),
				REL::VariantOffset(0x32F, 0x3E3, 0x32F),
				"TESObjectREFR node naming call B (15514 + 0x32F)",
				"medium",
				AsByteSpan(kValidationPrefix1D9B3F),
				kCallViaRax190,
				AsByteSpan(kValidationSuffix1D9B3F) },
			NodeNamingPatchSite{
				REL::VariantID(15524, 15701, kTESObjectNodeNamingCVR),
				REL::VariantOffset(0x126, 0x126, 0x126),
				"TESObjectREFR node naming call C (15524 + 0x126)",
				"strong",
				AsByteSpan(kValidationPrefix1DA6B6),
				kCallViaRax190,
				AsByteSpan(kValidationSuffix1DA6B6) },
		};
	}

	[[nodiscard]] std::uintptr_t GetEmptyStringAddress()
	{
		// The VR empty-string base was derived from skyrim_vr_address_library/sse_vr.csv
		// using the surrounding .rdata mapping because there is no published VR ID here yet.
		return REL::Relocation<std::uintptr_t>{ REL::VariantID(232409, 188366, kEmptyStringVR), REL::VariantOffset(0x6, 0x6, 0x6) }.address();
	}

	[[nodiscard]] std::array<std::uint8_t, kNodeNamingPatchSize> MakeNodeNamingPatch(
		std::uintptr_t patchSite,
		std::uintptr_t emptyString)
	{
		std::array<std::uint8_t, kNodeNamingPatchSize> patch{
			0x48,
			0x8D,
			0x05,
			0xDE,
			0xAD,
			0xBE,
			0xEF,
			0x90,
			0x90
		};

		const auto relative = static_cast<std::int32_t>(emptyString - patchSite) - 7;
		std::memcpy(patch.data() + 3, &relative, sizeof(relative));
		return patch;
	}

	[[nodiscard]] NodeNamingPatchPlan PrepareNodeNamingPatchPlan()
	{
		NodeNamingPatchPlan prepared{};
		const auto sites = GetNodeNamingPatchSites();
		prepared.requiredSiteCount = sites.size();
		prepared.patches.reserve(sites.size());

		const auto emptyString = GetEmptyStringAddress();
		if (emptyString == 0) {
			logs::warn("Skipping node naming compatibility patch because the empty-string address could not be resolved");
			return prepared;
		}
		if (REL::Module::IsVR()) {
			logs::info("Resolved Skyrim VR empty-string anchor at 0x{:X}", emptyString);
		}

		const auto textSegment = REL::Module::get().segment(REL::Segment::textx);
		if (textSegment.address() == 0 || textSegment.size() == 0) {
			logs::warn("Skipping node naming compatibility patch because the runtime text segment could not be resolved");
			return prepared;
		}

		prepared.emptyString = emptyString;
		for (const auto& site : sites) {
			const REL::Relocation<std::uintptr_t> target{ site.id, site.offset };
			if (target.address() == 0) {
				logs::warn(
					"Skipping node naming compatibility patch because {} could not be resolved for this runtime",
					site.label);
				continue;
			}

			PlannedNodeNamingPatch plannedPatch{};
			plannedPatch.target = target.address();
			plannedPatch.patch = MakeNodeNamingPatch(target.address(), emptyString);
			plannedPatch.label = site.label;
			plannedPatch.confidence = site.confidence;

			const auto actualOriginal = TryReadBytes(textSegment, { target.address(), plannedPatch.original.size() });
			if (!actualOriginal) {
				LogUnreadableValidationWindow(site.label, "target", target.address(), plannedPatch.original.size(), textSegment);
				continue;
			}
			std::copy(actualOriginal->begin(), actualOriginal->end(), plannedPatch.original.begin());

			if (REL::Module::IsVR()) {
				const auto prefixAddress = CheckedSub(target.address(), site.expectedPrefix.size());
				if (!prefixAddress) {
					LogUnreadableValidationWindow(site.label, "prefix", target.address(), site.expectedPrefix.size(), textSegment);
					continue;
				}

				const auto suffixAddress = CheckedAdd(target.address(), plannedPatch.original.size());
				if (!suffixAddress) {
					LogUnreadableValidationWindow(site.label, "suffix", target.address(), site.expectedSuffix.size(), textSegment);
					continue;
				}

				const auto actualPrefix = TryReadBytes(textSegment, { *prefixAddress, site.expectedPrefix.size() });
				if (!actualPrefix) {
					LogUnreadableValidationWindow(site.label, "prefix", *prefixAddress, site.expectedPrefix.size(), textSegment);
					continue;
				}
				const auto actualSuffix = TryReadBytes(textSegment, { *suffixAddress, site.expectedSuffix.size() });
				if (!actualSuffix) {
					LogUnreadableValidationWindow(site.label, "suffix", *suffixAddress, site.expectedSuffix.size(), textSegment);
					continue;
				}
				const auto prefixMatches = BytesMatch(site.expectedPrefix, *actualPrefix);
				const auto targetMatches = BytesMatch(site.expectedOriginal, plannedPatch.original);
				const auto suffixMatches = BytesMatch(site.expectedSuffix, *actualSuffix);
				if (!prefixMatches || !targetMatches || !suffixMatches) {
					logs::warn(
						"Skipping {} because the expected VR validation window did not match. prefix expected=[{}] actual=[{}] target expected=[{}] actual=[{}] suffix expected=[{}] actual=[{}]",
						site.label,
						FormatBytes(site.expectedPrefix),
						FormatBytes(*actualPrefix),
						FormatBytes(site.expectedOriginal),
						FormatBytes(plannedPatch.original),
						FormatBytes(site.expectedSuffix),
						FormatBytes(*actualSuffix));
					continue;
				}
			}

			prepared.patches.push_back(plannedPatch);
		}

		if (prepared.patches.empty()) {
			logs::warn("Skipping node naming compatibility patch because no validated sites were available for this runtime");
			return prepared;
		}

		if (prepared.patches.size() != prepared.requiredSiteCount) {
			logs::warn(
				"Prepared only partial node naming compatibility patch set for {}: validated {}/{} site(s)",
				Runtime::GetRuntimeName(),
				prepared.patches.size(),
				prepared.requiredSiteCount);
		}

		return prepared;
	}

	void ApplyNodeNamingCompatibility(const NodeNamingPatchPlan& prepared)
	{
		std::size_t appliedPatches = 0;
		try {
			for (const auto& plannedPatch : prepared.patches) {
				if (REL::Module::IsVR()) {
					logs::info(
						"Applying validated VR node naming patch {} at 0x{:X} using empty-string 0x{:X} (confidence={})",
						plannedPatch.label,
						plannedPatch.target,
						prepared.emptyString,
						plannedPatch.confidence);
				}

				REL::safe_write(plannedPatch.target, plannedPatch.patch.data(), plannedPatch.patch.size());
				++appliedPatches;
			}
		} catch (...) {
			for (std::size_t index = appliedPatches; index > 0; --index) {
				const auto& appliedPatch = prepared.patches[index - 1];
				REL::safe_write(appliedPatch.target, appliedPatch.original.data(), appliedPatch.original.size());
			}

			logs::critical("Node naming compatibility patch installation failed; reverted {} previously applied patch(es)", appliedPatches);
			throw;
		}

		logs::info("Installed node naming compatibility patches for {}", Runtime::GetRuntimeName());
	}
}

namespace Hooks::NodeNaming
{
	InstallResult Install()
	{
		InstallResult result{};
		result.requiredSites = GetNodeNamingPatchSites().size();

		static bool installed = false;
		if (installed) {
			result.active = true;
			result.validatedSites = result.requiredSites;
			return result;
		}

		auto prepared = PrepareNodeNamingPatchPlan();
		result.validatedSites = prepared.patches.size();
		result.requiredSites = prepared.requiredSiteCount;

		if (prepared.patches.size() != prepared.requiredSiteCount) {
			return result;
		}

		ApplyNodeNamingCompatibility(prepared);
		installed = true;

		result.active = true;
		result.validatedSites = result.requiredSites;
		return result;
	}
}
