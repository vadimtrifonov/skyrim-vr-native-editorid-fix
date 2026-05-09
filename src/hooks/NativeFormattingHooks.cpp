#include "hooks/NativeFormattingHooks.h"

#include "hooks/Bytes.h"
#include "pch.h"
#include "runtime/RuntimeInfo.h"

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <span>
#include <vector>

namespace
{
	constexpr std::size_t kIndirectCallSize = 6;
	constexpr std::size_t kDetourPointerSlots = 2;
	constexpr std::uint8_t kCallOpcode = 0xFF;
	constexpr std::uint8_t kCallIndirectRipModRm = 0x15;

	enum class FormattingBufferSize : std::uint16_t
	{
		Normal = 0x400,
		Small = 0x128
	};

	struct RelocatedFormattingCallSite
	{
		REL::ID id;
		std::ptrdiff_t offset;
		FormattingBufferSize bufferSize;
		std::string_view label;
	};

	struct VrFormattingCallSite
	{
		std::size_t rva;
		REL::ID containingId;
		std::ptrdiff_t offset;
		bool useContainingId;
		FormattingBufferSize bufferSize;
		std::string_view label;
	};

	struct ResolvedFormattingCallSite
	{
		std::uintptr_t target = 0;
		FormattingBufferSize bufferSize = FormattingBufferSize::Normal;
		std::string_view label;
	};

	using Hooks::Bytes::FormatBytes;
	using Hooks::Bytes::SegmentEnd;
	using Hooks::Bytes::TryReadBytes;

	[[nodiscard]] std::uintptr_t ResolveCallTarget(std::uintptr_t callSite, std::span<const std::uint8_t, kIndirectCallSize> callBytes)
	{
		std::int32_t displacement = 0;
		std::memcpy(&displacement, callBytes.data() + 2, sizeof(displacement));
		return callSite + kIndirectCallSize + displacement;
	}

	[[nodiscard]] bool IsIndirectCall(std::span<const std::uint8_t> bytes)
	{
		return bytes.size() == kIndirectCallSize && bytes[0] == kCallOpcode && bytes[1] == kCallIndirectRipModRm;
	}

	[[nodiscard]] const void* SelectDetour(FormattingBufferSize bufferSize) noexcept;

	int VsprintfDetour(unsigned __int64 options, char* buffer, std::size_t, char const* format, _locale_t locale, va_list argList)
	{
		return __stdio_common_vsprintf(options, buffer, static_cast<std::size_t>(FormattingBufferSize::Normal), format, locale, argList);
	}

	int SmallVsprintfDetour(unsigned __int64 options, char* buffer, std::size_t, char const* format, _locale_t locale, va_list argList)
	{
		return __stdio_common_vsprintf(options, buffer, static_cast<std::size_t>(FormattingBufferSize::Small), format, locale, argList);
	}

	[[nodiscard]] const void* SelectDetour(FormattingBufferSize bufferSize) noexcept
	{
		switch (bufferSize) {
		case FormattingBufferSize::Normal:
			return reinterpret_cast<const void*>(&VsprintfDetour);
		case FormattingBufferSize::Small:
			return reinterpret_cast<const void*>(&SmallVsprintfDetour);
		}

		return nullptr;
	}

	[[nodiscard]] std::string_view ToString(FormattingBufferSize bufferSize)
	{
		switch (bufferSize) {
		case FormattingBufferSize::Normal:
			return "0x400";
		case FormattingBufferSize::Small:
			return "0x128";
		}

		return "unknown";
	}

	[[nodiscard]] std::span<const RelocatedFormattingCallSite> GetSEFormattingCallSites()
	{
		static constexpr std::array<RelocatedFormattingCallSite, 10> kSites{
			RelocatedFormattingCallSite{ REL::ID(10983), 0x44, FormattingBufferSize::Normal, "SE normal formatting call 0 (10983 + 0x44)" },
			RelocatedFormattingCallSite{ REL::ID(13837), 0x44, FormattingBufferSize::Normal, "SE normal formatting call 1 (13837 + 0x44)" },
			RelocatedFormattingCallSite{ REL::ID(15702), 0x48, FormattingBufferSize::Normal, "SE normal formatting call 2 (15702 + 0x48)" },
			RelocatedFormattingCallSite{ REL::ID(21483), 0x50, FormattingBufferSize::Normal, "SE normal formatting call 3 (21483 + 0x50)" },
			RelocatedFormattingCallSite{ REL::ID(22856), 0x44, FormattingBufferSize::Normal, "SE normal formatting call 4 (22856 + 0x44)" },
			RelocatedFormattingCallSite{ REL::ID(34780), 0x5B, FormattingBufferSize::Normal, "SE normal formatting call 5 (34780 + 0x5B)" },
			RelocatedFormattingCallSite{ REL::ID(50180), 0xDB, FormattingBufferSize::Normal, "SE normal formatting call 6 (50180 + 0xDB)" },
			RelocatedFormattingCallSite{ REL::ID(56708), 0x48, FormattingBufferSize::Normal, "SE normal formatting call 7 (56708 + 0x48)" },
			RelocatedFormattingCallSite{ REL::ID(91676), 0x44, FormattingBufferSize::Normal, "SE normal formatting call 8 (91676 + 0x44)" },
			RelocatedFormattingCallSite{ REL::ID(22882), 0x4A, FormattingBufferSize::Small, "SE small formatting call (22882 + 0x4A)" }
		};
		return kSites;
	}

	[[nodiscard]] std::span<const RelocatedFormattingCallSite> GetAEFormattingCallSites()
	{
		static constexpr std::array<RelocatedFormattingCallSite, 13> kSites{
			RelocatedFormattingCallSite{ REL::ID(440412), 0x42, FormattingBufferSize::Normal, "AE normal formatting call 0 (440412 + 0x42)" },
			RelocatedFormattingCallSite{ REL::ID(440413), 0x42, FormattingBufferSize::Normal, "AE normal formatting call 1 (440413 + 0x42)" },
			RelocatedFormattingCallSite{ REL::ID(440474), 0x5C, FormattingBufferSize::Normal, "AE normal formatting call 2 (440474 + 0x5C)" },
			RelocatedFormattingCallSite{ REL::ID(13906), 0x51, FormattingBufferSize::Normal, "AE normal formatting call 3 (13906 + 0x51)" },
			RelocatedFormattingCallSite{ REL::ID(23305), 0x51, FormattingBufferSize::Normal, "AE normal formatting call 4 (23305 + 0x51)" },
			RelocatedFormattingCallSite{ REL::ID(35684), 0x65, FormattingBufferSize::Normal, "AE normal formatting call 5 (35684 + 0x65)" },
			RelocatedFormattingCallSite{ REL::ID(35685), 0x5E, FormattingBufferSize::Normal, "AE normal formatting call 6 (35685 + 0x5E)" },
			RelocatedFormattingCallSite{ REL::ID(51110), 0x287, FormattingBufferSize::Normal, "AE normal formatting call 7 (51110 + 0x287)" },
			RelocatedFormattingCallSite{ REL::ID(57117), 0x48, FormattingBufferSize::Normal, "AE normal formatting call 8 (57117 + 0x48)" },
			RelocatedFormattingCallSite{ REL::ID(69540), 0x51, FormattingBufferSize::Normal, "AE normal formatting call 9 (69540 + 0x51)" },
			RelocatedFormattingCallSite{ REL::ID(94541), 0x51, FormattingBufferSize::Normal, "AE normal formatting call 10 (94541 + 0x51)" },
			RelocatedFormattingCallSite{ REL::ID(21962), 0x50, FormattingBufferSize::Small, "AE small formatting call 0 (21962 + 0x50)" },
			RelocatedFormattingCallSite{ REL::ID(23333), 0x4A, FormattingBufferSize::Small, "AE small formatting call 1 (23333 + 0x4A)" }
		};
		return kSites;
	}

	[[nodiscard]] std::span<const VrFormattingCallSite> GetVRFormattingCallSites()
	{
		static constexpr std::array<VrFormattingCallSite, 9> kSites{
			// Validated unbounded formatting call sites for Skyrim VR 1.4.15.0.
			// These addresses are RVAs of the 6-byte indirect-call instruction sites; the call bytes are re-read before patching.
			// The already-bounded call-site RVAs at 0xA043C3, 0xA04423, 0xA121E3, 0xF32587, and 0xF32683 are intentionally excluded.
			VrFormattingCallSite{ 0x10A7D4, REL::ID(0), 0, false, FormattingBufferSize::Normal, "VR normal formatting call 0 (0x10A7D4)" },
			VrFormattingCallSite{ 0x189934, REL::ID(0), 0, false, FormattingBufferSize::Normal, "VR normal formatting call 1 (0x189934)" },
			VrFormattingCallSite{ 0x1E54B8, REL::ID(0), 0, false, FormattingBufferSize::Normal, "VR normal formatting call 2 (0x1E54B8)" },
			VrFormattingCallSite{ 0x2FF0B0, REL::ID(0), 0, false, FormattingBufferSize::Normal, "VR normal formatting call 3 (0x2FF0B0)" },
			VrFormattingCallSite{ 0x336564, REL::ID(0), 0, false, FormattingBufferSize::Normal, "VR normal formatting call 4 (0x336564)" },
			VrFormattingCallSite{ 0x58BB1B, REL::ID(0), 0, false, FormattingBufferSize::Normal, "VR normal formatting call 5 (0x58BB1B)" },
			VrFormattingCallSite{ 0, REL::ID(50180), 0xDB, true, FormattingBufferSize::Normal, "VR normal formatting call 6 (50180 + 0xDB)" },
			VrFormattingCallSite{ 0xA04498, REL::ID(0), 0, false, FormattingBufferSize::Normal, "VR normal formatting call 7 (0xA04498)" },
			VrFormattingCallSite{ 0x33852A, REL::ID(0), 0, false, FormattingBufferSize::Small, "VR small formatting call (0x33852A)" }
		};
		return kSites;
	}

	[[nodiscard]] std::vector<ResolvedFormattingCallSite> GetFormattingCallSites()
	{
		std::vector<ResolvedFormattingCallSite> sites;

		if (REL::Module::IsVR()) {
			const auto vrSites = GetVRFormattingCallSites();
			sites.reserve(vrSites.size());
			for (const auto& site : vrSites) {
				std::uintptr_t target = 0;
				if (site.useContainingId) {
					const REL::Relocation<std::uintptr_t> relocation{ site.containingId, site.offset };
					target = relocation.address();
				} else {
					target = REL::Offset(site.rva).address();
				}

				sites.push_back({ target, site.bufferSize, site.label });
			}
			return sites;
		}

		const auto relocatedSites = REL::Module::IsAE() ? GetAEFormattingCallSites() : GetSEFormattingCallSites();
		sites.reserve(relocatedSites.size());
		for (const auto& site : relocatedSites) {
			const REL::Relocation<std::uintptr_t> target{ site.id, site.offset };
			sites.push_back({ target.address(), site.bufferSize, site.label });
		}

		return sites;
	}

	[[nodiscard]] bool ValidateFormattingCallSite(const ResolvedFormattingCallSite& site, const REL::Segment& textSegment)
	{
		if (site.target == 0) {
			logs::warn("Skipping {} because its address resolved to zero", site.label);
			return false;
		}

		const auto originalBytes = TryReadBytes(textSegment, { site.target, kIndirectCallSize });
		if (!originalBytes) {
			logs::warn(
				"Skipping {} because its patch window could not be read from the loaded text segment. address=0x{:X} size={} text=[0x{:X},0x{:X})",
				site.label,
				site.target,
				kIndirectCallSize,
				textSegment.address(),
				SegmentEnd(textSegment));
			return false;
		}

		if (!IsIndirectCall(*originalBytes)) {
			logs::warn(
				"Skipping {} because expected a 6-byte indirect call but found [{}] at 0x{:X}",
				site.label,
				FormatBytes(*originalBytes),
				site.target);
			return false;
		}

		std::array<std::uint8_t, kIndirectCallSize> callBytes{};
		std::memcpy(callBytes.data(), originalBytes->data(), callBytes.size());
		logs::info(
			"Validated native formatting call {} at 0x{:X} -> import 0x{:X} (bound={})",
			site.label,
			site.target,
			ResolveCallTarget(site.target, callBytes),
			ToString(site.bufferSize));
		return true;
	}
}

namespace Hooks::NativeFormatting
{
	std::size_t GetTrampolineSize()
	{
		return GetFormattingCallSites().empty() ? 0 : kDetourPointerSlots * sizeof(std::uintptr_t);
	}

	void Install()
	{
		static bool installed = false;
		if (installed) {
			return;
		}

		const auto sites = GetFormattingCallSites();
		if (sites.empty()) {
			logs::warn("No native formatting call sites are defined for {}", Runtime::GetRuntimeName());
			return;
		}

		const auto textSegment = REL::Module::get().segment(REL::Segment::textx);
		if (textSegment.address() == 0 || textSegment.size() == 0) {
			logs::warn("Skipping native formatting bounds patches because the runtime text segment could not be resolved");
			return;
		}

		std::size_t validated = 0;
		for (const auto& site : sites) {
			if (!ValidateFormattingCallSite(site, textSegment)) {
				continue;
			}

			SKSE::GetTrampoline().write_call<kIndirectCallSize>(site.target, SelectDetour(site.bufferSize));
			++validated;
		}

		logs::info(
			"Installed native formatting bounds patches for {}: {}/{} site(s)",
			Runtime::GetRuntimeName(),
			validated,
			sites.size());

		installed = true;
	}
}
