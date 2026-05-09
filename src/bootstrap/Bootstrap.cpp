#include "bootstrap/Bootstrap.h"

#include "hooks/HelpCommandHooks.h"
#include "hooks/LookupHooks.h"
#include "hooks/NativeFormattingHooks.h"
#include "hooks/NodeNamingHooks.h"
#include "lookup/LookupMode.h"
#include "lookup/LookupTable.h"
#include "pch.h"
#include "runtime/RuntimeInfo.h"
#include "settings/Settings.h"

namespace
{
	std::string_view ToString(LookupMode::Value mode)
	{
		using enum LookupMode::Value;

		switch (mode) {
		case None:
			return "None";
		case ExternalOnly:
			return "ExternalOnly";
		case NativeNoNodes:
			return "NativeNoNodes";
		case Native:
			return "Native";
		}

		return "Unknown";
	}

	[[nodiscard]] bool IsAnyTraceEnabled(const Settings::Values& settings)
	{
		return settings.traceSetFormEditorID ||
		       settings.traceNativeEditorIDMap ||
		       settings.traceGetFormEditorID ||
		       settings.traceGetReferenceEditorID;
	}

	LookupMode::Value InstallConfiguredHooks(const Settings::Values& settings, LookupMode::Value configuredMode)
	{
		if (settings.helpCommandSwitchNamePriority) {
			Hooks::HelpCommand::Install();
		} else {
			logs::info("Help command full-name priority patch disabled by settings");
		}

		bool nodeNamingCompatibilityActive = false;
		if (configuredMode == LookupMode::Value::NativeNoNodes) {
			const auto nodeNamingInstallResult = Hooks::NodeNaming::Install();
			nodeNamingCompatibilityActive = nodeNamingInstallResult.active;

			logs::info(
				"Node naming compatibility patches for {}: active={} ({}/{})",
				Runtime::GetRuntimeName(),
				nodeNamingInstallResult.active,
				nodeNamingInstallResult.validatedSites,
				nodeNamingInstallResult.requiredSites);

			if (!nodeNamingCompatibilityActive) {
				throw std::runtime_error("Configured NativeNoNodes requires active node naming compatibility patches.");
			}
		}

		const auto formattingTrampolineSize = settings.boundsCheckNativeFormatting ? Hooks::NativeFormatting::GetTrampolineSize() : 0;
		const auto lookupTrampolineSize = Hooks::Lookup::GetTrampolineSize(configuredMode);
		const auto trampolineSize = formattingTrampolineSize + lookupTrampolineSize;
		if (trampolineSize != 0) {
			SKSE::AllocTrampoline(trampolineSize);
		}

		if (settings.boundsCheckNativeFormatting) {
			Hooks::NativeFormatting::Install();
		} else {
			logs::info("Native formatting bounds patches disabled by settings");
		}

		const auto lookupInstallResult = Hooks::Lookup::Install(configuredMode, settings);
		if (lookupInstallResult.setterHookActive) {
			logs::info("Installed lookup setter hook for configured mode {}", ToString(configuredMode));
		} else {
			logs::info("No lookup setter hook installed for configured mode {}", ToString(configuredMode));
		}

		return LookupMode::ResolveEffective(
			configuredMode,
			lookupInstallResult.nativeGettersActive,
			nodeNamingCompatibilityActive);
	}
}

namespace Bootstrap
{
	void Initialize()
	{
		logs::info("Initializing Native EditorID Fix VR on {} runtime {}", Runtime::GetRuntimeName(), REL::Module::get().version());

		const auto settingsPath = Settings::GetDefaultPath();
		const auto settingsExists = std::filesystem::exists(settingsPath);
		if (settingsExists) {
			logs::info("Loading settings from {}", settingsPath.string());
		} else {
			logs::warn("Settings file not found at {}; using built-in defaults", settingsPath.string());
		}

		const auto loadResult = Settings::LoadWithDiagnostics(settingsPath);
		for (const auto& warning : loadResult.warnings) {
			logs::warn("{}", warning);
		}

		const auto configuredMode = LookupMode::FromSettings(loadResult.settings);
		LookupMode::SetEffective(configuredMode);
		LookupTable::Reset({ configuredMode != LookupMode::Value::None, loadResult.settings.maxEditorIDLength });

		const auto effectiveMode = InstallConfiguredHooks(loadResult.settings, configuredMode);
		LookupMode::SetEffective(effectiveMode);

		logs::info(
			"Lookup mode configured={} effective={} (lookup_table={}, native_lookup={}, max_len={})",
			ToString(configuredMode),
			ToString(effectiveMode),
			loadResult.settings.enableLookupTable,
			loadResult.settings.enableNativeEditorIDLookup,
			loadResult.settings.maxEditorIDLength);

		if (IsAnyTraceEnabled(loadResult.settings)) {
			logs::info(
				"Trace config set={} map={} get_form={} get_ref={}",
				loadResult.settings.traceSetFormEditorID,
				loadResult.settings.traceNativeEditorIDMap,
				loadResult.settings.traceGetFormEditorID,
				loadResult.settings.traceGetReferenceEditorID);
		}
	}
}
