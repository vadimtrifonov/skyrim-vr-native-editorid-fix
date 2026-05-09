#include "pch.h"

#include "api/NativeEditorIDFixAPI.hpp"
#include "lookup/LookupMode.h"
#include "lookup/LookupTable.h"
#include "lookup/LookupTableMutation.h"
#include "settings/Settings.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

extern "C" NEIF::EditorIDLookupState NEIF_GetEditorIDLookupState();
extern "C" const char* NEIF_GetEditorID(NEIF::FormInfo form);

namespace
{
	using namespace std::string_view_literals;
	using namespace std::chrono_literals;

	struct TestCase
	{
		const char* name;
		void (*run)();
	};

	struct TemporaryIniFile
	{
		explicit TemporaryIniFile(std::string_view contents) :
			path(MakeUniquePath())
		{
			std::ofstream file(path, std::ios::binary);
			if (!file) {
				throw std::runtime_error("failed to create temporary ini file");
			}

			file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
			if (!file) {
				throw std::runtime_error("failed to write temporary ini file");
			}
		}

		~TemporaryIniFile()
		{
			std::error_code error;
			std::filesystem::remove(path, error);
		}

		[[nodiscard]] static std::filesystem::path MakeUniquePath()
		{
			static std::size_t counter = 0;

			auto path = std::filesystem::temp_directory_path();
			path /= "NativeEditorIDFix-tests-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(counter++) + ".ini";
			return path;
		}

		std::filesystem::path path;
	};

	void Require(bool condition, std::string_view message)
	{
		if (!condition) {
			throw std::runtime_error(std::string(message));
		}
	}

	template <class T>
	void RequireEqual(const T& actual, const T& expected, std::string_view message)
	{
		if (actual != expected) {
			throw std::runtime_error(std::string(message));
		}
	}

	void RequireEqual(std::string_view actual, std::string_view expected, std::string_view message)
	{
		if (actual != expected) {
			throw std::runtime_error(
				std::string(message) + " (expected '" + std::string(expected) + "', got '" + std::string(actual) + "')");
		}
	}

	template <class TException = std::exception, class TCallable>
	void RequireThrows(TCallable&& callable, std::string_view message)
	{
		try {
			callable();
		} catch (const TException&) {
			return;
		} catch (...) {
			throw std::runtime_error(std::string(message) + " (unexpected exception type)");
		}

		throw std::runtime_error(std::string(message) + " (no exception thrown)");
	}

	template <class TPointer>
	TPointer RequireNotNull(TPointer value, std::string_view message)
	{
		if (value == nullptr) {
			throw std::runtime_error(std::string(message));
		}

		return value;
	}

	std::filesystem::path GetExecutablePath()
	{
		std::wstring buffer(32768, L'\0');
		const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (length == 0 || length == buffer.size()) {
			throw std::runtime_error("failed to resolve current executable path");
		}

		return std::filesystem::path(std::wstring_view(buffer.data(), length));
	}

	std::filesystem::path GetBuildOutputDirectory()
	{
		return GetExecutablePath().parent_path();
	}

	DWORD RunChildScenario(std::wstring_view scenario)
	{
		auto commandLine = L"\"" + GetExecutablePath().native() + L"\" " + std::wstring(scenario);

		STARTUPINFOW startupInfo{};
		PROCESS_INFORMATION processInfo{};
		startupInfo.cb = sizeof(startupInfo);

		if (!CreateProcessW(
				nullptr,
				commandLine.data(),
				nullptr,
				nullptr,
				FALSE,
				0,
				nullptr,
				GetBuildOutputDirectory().c_str(),
				&startupInfo,
				&processInfo)) {
			throw std::runtime_error("failed to spawn child test scenario");
		}

		CloseHandle(processInfo.hThread);
		const auto waitResult = WaitForSingleObject(processInfo.hProcess, INFINITE);
		if (waitResult != WAIT_OBJECT_0) {
			CloseHandle(processInfo.hProcess);
			throw std::runtime_error("child test scenario wait failed");
		}

		DWORD exitCode = EXIT_FAILURE;
		if (!GetExitCodeProcess(processInfo.hProcess, &exitCode)) {
			CloseHandle(processInfo.hProcess);
			throw std::runtime_error("failed to read child test scenario exit code");
		}

		CloseHandle(processInfo.hProcess);
		return exitCode;
	}

	HMODULE LoadPluginModule()
	{
		const auto pluginPath = GetBuildOutputDirectory() / L"NativeEditorIDFix.dll";
		const auto module = LoadLibraryW(pluginPath.c_str());
		if (module == nullptr) {
			throw std::runtime_error("failed to load NativeEditorIDFix.dll from the build output directory");
		}

		return module;
	}

	void TestSettingsMissingFileUsesDefaults()
	{
		const auto missingPath = TemporaryIniFile::MakeUniquePath();
		std::error_code error;
		std::filesystem::remove(missingPath, error);

		const auto settings = Settings::Load(missingPath);
		RequireEqual(settings.enableLookupTable, true, "missing file should preserve EnableLookupTable default");
		RequireEqual(
			settings.enableNativeEditorIDLookup,
			true,
			"missing file should preserve EnableNativeEditorIDLookup default");
		RequireEqual(settings.maxEditorIDLength, static_cast<std::size_t>(128), "missing file should preserve MaxEditorIDLength default");
		RequireEqual(
			settings.boundsCheckNativeFormatting,
			true,
			"missing file should preserve BoundsCheckNativeFormatting default");
		RequireEqual(
			settings.excludeEditorIDFromNodeNaming,
			true,
			"missing file should preserve ExcludeEditorIDFromNodeNaming default");
		RequireEqual(
			settings.helpCommandSwitchNamePriority,
			true,
			"missing file should preserve HelpCommandSwitchNamePriority default");
		RequireEqual(settings.traceSetFormEditorID, false, "missing file should preserve TraceSetFormEditorID default");
		RequireEqual(settings.traceNativeEditorIDMap, false, "missing file should preserve TraceNativeEditorIDMap default");
		RequireEqual(settings.traceGetFormEditorID, false, "missing file should preserve TraceGetFormEditorID default");
		RequireEqual(settings.traceGetReferenceEditorID, false, "missing file should preserve TraceGetReferenceEditorID default");
	}

	void TestSettingsMissingKeysPreserveDefaults()
	{
		const TemporaryIniFile ini(
			"[Main]\r\n"
			"EnableNativeEditorIDLookup=on\r\n"
			"\r\n"
			"[Patches]\r\n"
			"HelpCommandSwitchNamePriority=off\r\n");

		const auto settings = Settings::Load(ini.path);
		RequireEqual(settings.enableLookupTable, true, "missing EnableLookupTable should preserve default");
		RequireEqual(settings.enableNativeEditorIDLookup, true, "present EnableNativeEditorIDLookup should be parsed");
		RequireEqual(settings.maxEditorIDLength, static_cast<std::size_t>(128), "missing MaxEditorIDLength should preserve default");
		RequireEqual(settings.boundsCheckNativeFormatting, true, "missing BoundsCheckNativeFormatting should preserve default");
		RequireEqual(
			settings.excludeEditorIDFromNodeNaming,
			true,
			"missing ExcludeEditorIDFromNodeNaming should preserve default");
		RequireEqual(settings.helpCommandSwitchNamePriority, false, "present HelpCommandSwitchNamePriority should be parsed");
		RequireEqual(settings.traceSetFormEditorID, false, "missing TraceSetFormEditorID should preserve default");
		RequireEqual(settings.traceNativeEditorIDMap, false, "missing TraceNativeEditorIDMap should preserve default");
		RequireEqual(settings.traceGetFormEditorID, false, "missing TraceGetFormEditorID should preserve default");
		RequireEqual(settings.traceGetReferenceEditorID, false, "missing TraceGetReferenceEditorID should preserve default");
	}

	void TestSettingsMalformedValuesFallBackPerKey()
	{
		const TemporaryIniFile ini(
			"[Main]\r\n"
			"EnableLookupTable=garbage\r\n"
			"EnableNativeEditorIDLookup=yes\r\n"
			"MaxEditorIDLength=12x\r\n"
			"\r\n"
			"[Patches]\r\n"
			"BoundsCheckNativeFormatting=maybe\r\n"
			"ExcludeEditorIDFromNodeNaming=0\r\n"
			"HelpCommandSwitchNamePriority=invalid\r\n");

		const auto loadResult = Settings::LoadWithDiagnostics(ini.path);
		const auto& settings = loadResult.settings;
		RequireEqual(settings.enableLookupTable, true, "malformed EnableLookupTable should fall back");
		RequireEqual(settings.enableNativeEditorIDLookup, true, "valid EnableNativeEditorIDLookup should still parse");
		RequireEqual(settings.maxEditorIDLength, static_cast<std::size_t>(128), "malformed MaxEditorIDLength should fall back");
		RequireEqual(
			settings.boundsCheckNativeFormatting,
			true,
			"malformed BoundsCheckNativeFormatting should fall back");
		RequireEqual(
			settings.excludeEditorIDFromNodeNaming,
			false,
			"valid ExcludeEditorIDFromNodeNaming should still parse");
		RequireEqual(
			settings.helpCommandSwitchNamePriority,
			true,
			"malformed HelpCommandSwitchNamePriority should fall back");
		RequireEqual(loadResult.warnings.size(), static_cast<std::size_t>(4), "malformed values should produce one warning per invalid key");
		Require(loadResult.warnings[0].find("EnableLookupTable") != std::string::npos, "warning should mention EnableLookupTable");
		Require(loadResult.warnings[1].find("MaxEditorIDLength") != std::string::npos, "warning should mention MaxEditorIDLength");
		Require(loadResult.warnings[2].find("BoundsCheckNativeFormatting") != std::string::npos, "warning should mention BoundsCheckNativeFormatting");
		Require(loadResult.warnings[3].find("HelpCommandSwitchNamePriority") != std::string::npos, "warning should mention HelpCommandSwitchNamePriority");
	}

	void TestSettingsDebugTraceFlagsParseAndFallBack()
	{
		const TemporaryIniFile ini(
			"[Debug]\r\n"
			"TraceSetFormEditorID=on\r\n"
			"TraceNativeEditorIDMap=yes\r\n"
			"TraceGetFormEditorID=garbage\r\n"
			"TraceGetReferenceEditorID=0\r\n");

		const auto loadResult = Settings::LoadWithDiagnostics(ini.path);
		const auto& settings = loadResult.settings;

		RequireEqual(settings.traceSetFormEditorID, true, "TraceSetFormEditorID should parse");
		RequireEqual(settings.traceNativeEditorIDMap, true, "TraceNativeEditorIDMap should parse");
		RequireEqual(settings.traceGetFormEditorID, false, "malformed TraceGetFormEditorID should fall back");
		RequireEqual(settings.traceGetReferenceEditorID, false, "TraceGetReferenceEditorID should parse");
		RequireEqual(loadResult.warnings.size(), static_cast<std::size_t>(1), "only invalid trace keys should warn");
		Require(loadResult.warnings[0].find("TraceGetFormEditorID") != std::string::npos, "warning should mention TraceGetFormEditorID");
	}

	void TestConfiguredLookupStateMatrix()
	{
		{
			Settings::Values settings{};
			settings.enableLookupTable = false;
			LookupTable::Reset({ settings.enableLookupTable, settings.maxEditorIDLength });

			RequireEqual(LookupMode::FromSettings(settings), LookupMode::Value::None, "configured lookup state should be None");
		}

		{
			Settings::Values settings{};
			settings.enableNativeEditorIDLookup = false;
			LookupTable::Reset({ settings.enableLookupTable, settings.maxEditorIDLength });

			RequireEqual(
				LookupMode::FromSettings(settings),
				LookupMode::Value::ExternalOnly,
				"configured lookup state should be ExternalOnly");
		}

		{
			Settings::Values settings{};
			settings.enableNativeEditorIDLookup = true;
			settings.excludeEditorIDFromNodeNaming = true;
			LookupTable::Reset({ settings.enableLookupTable, settings.maxEditorIDLength });

			RequireEqual(
				LookupMode::FromSettings(settings),
				LookupMode::Value::NativeNoNodes,
				"configured lookup state should be NativeNoNodes");
		}

		{
			Settings::Values settings{};
			settings.enableNativeEditorIDLookup = true;
			settings.excludeEditorIDFromNodeNaming = false;
			LookupTable::Reset({ settings.enableLookupTable, settings.maxEditorIDLength });

			RequireEqual(LookupMode::FromSettings(settings), LookupMode::Value::Native, "configured lookup state should be Native");
		}
	}

	void TestEffectiveLookupStateResolutionRequiresSatisfiedNativeContracts()
	{
		{
			Settings::Values settings{};
			settings.enableLookupTable = false;
			LookupTable::Reset({ settings.enableLookupTable, settings.maxEditorIDLength });
			LookupMode::SetEffective(
				LookupMode::ResolveEffective(LookupMode::FromSettings(settings), false, false));

			RequireEqual(LookupMode::GetEffective(), LookupMode::Value::None, "effective lookup state should be None");
			RequireEqual(NEIF_GetEditorIDLookupState(), NEIF::EditorIDLookupState::None, "exported effective lookup state should be None");
		}

		{
			Settings::Values settings{};
			settings.enableNativeEditorIDLookup = false;
			LookupTable::Reset({ settings.enableLookupTable, settings.maxEditorIDLength });
			LookupMode::SetEffective(
				LookupMode::ResolveEffective(LookupMode::FromSettings(settings), false, false));

			RequireEqual(LookupMode::GetEffective(), LookupMode::Value::ExternalOnly, "effective lookup state should be ExternalOnly");
			RequireEqual(
				NEIF_GetEditorIDLookupState(),
				NEIF::EditorIDLookupState::ExternalOnly,
				"exported effective lookup state should be ExternalOnly");
		}

		{
			Settings::Values settings{};
			settings.enableNativeEditorIDLookup = true;
			settings.excludeEditorIDFromNodeNaming = true;
			LookupTable::Reset({ settings.enableLookupTable, settings.maxEditorIDLength });
			RequireThrows<std::runtime_error>(
				[&] {
					(void)LookupMode::ResolveEffective(LookupMode::FromSettings(settings), false, false);
				},
				"NativeNoNodes should not silently downgrade when native getters are inactive");
		}

		{
			Settings::Values settings{};
			settings.enableNativeEditorIDLookup = true;
			settings.excludeEditorIDFromNodeNaming = false;
			LookupTable::Reset({ settings.enableLookupTable, settings.maxEditorIDLength });
			LookupMode::SetEffective(
				LookupMode::ResolveEffective(LookupMode::FromSettings(settings), true, false));

			RequireEqual(
				LookupMode::GetEffective(),
				LookupMode::Value::Native,
				"effective lookup state should be Native when native getters are restored");
			RequireEqual(
				NEIF_GetEditorIDLookupState(),
				NEIF::EditorIDLookupState::Native,
				"exported effective lookup state should be Native when native getters are restored");
		}

		{
			Settings::Values settings{};
			settings.enableNativeEditorIDLookup = true;
			settings.excludeEditorIDFromNodeNaming = true;
			LookupTable::Reset({ settings.enableLookupTable, settings.maxEditorIDLength });
			RequireThrows<std::runtime_error>(
				[&] {
					(void)LookupMode::ResolveEffective(LookupMode::FromSettings(settings), true, false);
				},
				"NativeNoNodes should not silently downgrade when node naming compatibility is inactive");
		}

		{
			Settings::Values settings{};
			settings.enableNativeEditorIDLookup = true;
			settings.excludeEditorIDFromNodeNaming = true;
			LookupTable::Reset({ settings.enableLookupTable, settings.maxEditorIDLength });
			LookupMode::SetEffective(
				LookupMode::ResolveEffective(LookupMode::FromSettings(settings), true, true));

			RequireEqual(
				LookupMode::GetEffective(),
				LookupMode::Value::NativeNoNodes,
				"effective lookup state should be NativeNoNodes when getter and node naming compatibility are both active");
			RequireEqual(
				NEIF_GetEditorIDLookupState(),
				NEIF::EditorIDLookupState::NativeNoNodes,
				"exported effective lookup state should be NativeNoNodes when getter and node naming compatibility are both active");
		}

		{
			Settings::Values settings{};
			settings.enableNativeEditorIDLookup = true;
			settings.excludeEditorIDFromNodeNaming = false;
			LookupTable::Reset({ settings.enableLookupTable, settings.maxEditorIDLength });
			RequireThrows<std::runtime_error>(
				[&] {
					(void)LookupMode::ResolveEffective(LookupMode::FromSettings(settings), false, false);
				},
				"Native should not silently downgrade when native getters are inactive");
		}
	}

	void TestMissingLookupReturnsEmptyString()
	{
		LookupTable::Reset({});
		RequireEqual(std::string_view(NEIF_GetEditorID({ 0x1234u, 0x01u })), ""sv, "missing lookup should return empty string");
	}

	void TestSetterIgnoresNullAndEmpty()
	{
		LookupTable::Reset({});

		RequireEqual(LookupTable::SetEditorID(0x1234u, 0x01u, nullptr).status, LookupTable::SetEditorIDStatus::Ignored, "null editor ID should be rejected");
		Require(!LookupTable::FindEditorID(0x1234u, 0x01u), "null editor ID should be ignored");

		RequireEqual(LookupTable::SetEditorID(0x1234u, 0x01u, "").status, LookupTable::SetEditorIDStatus::Ignored, "empty editor ID should be rejected");
		Require(!LookupTable::FindEditorID(0x1234u, 0x01u), "empty editor ID should be ignored");
	}

	void TestSetterIgnoresStoryManagerNodes()
	{
		LookupTable::Reset({});
		RequireEqual(LookupTable::SetEditorID(0x4321u, 0x72u, "StoryNode").status, LookupTable::SetEditorIDStatus::Ignored, "story manager node editor ID should be rejected");
		Require(!LookupTable::FindEditorID(0x4321u, 0x72u), "story manager nodes should be ignored");
	}

	void TestSetterRespectsMaxEditorIDLength()
	{
		Settings::Values settings{};
		settings.maxEditorIDLength = 3;
		LookupTable::Reset({ settings.enableLookupTable, settings.maxEditorIDLength });

		const auto setResult = LookupTable::SetEditorID(0x2222u, 0x01u, "abcdef");
		RequireEqual(setResult.status, LookupTable::SetEditorIDStatus::Changed, "truncated editor ID should be stored");
		Require(!setResult.previousEditorID, "first stored editor ID should not report a previous value");
		const auto* currentEditorID = RequireNotNull(setResult.currentEditorID, "stored editor ID should report the current value");
		const auto* editorID = RequireNotNull(LookupTable::FindEditorID(0x2222u, 0x01u), "truncated editor ID should be stored");

		RequireEqual(*editorID, std::string("abc"), "editor ID should be truncated");
		RequireEqual(*currentEditorID, std::string("abc"), "reported current editor ID should match stored truncation");
		RequireEqual(std::string_view(NEIF_GetEditorID({ 0x2222u, 0x01u })), "abc"sv, "exported editor ID should be truncated");
	}

	void TestSetterAllowsUnlimitedEditorIDLength()
	{
		Settings::Values settings{};
		settings.maxEditorIDLength = 0;
		LookupTable::Reset({ settings.enableLookupTable, settings.maxEditorIDLength });

		RequireEqual(LookupTable::SetEditorID(0x3333u, 0x01u, "abcdef").status, LookupTable::SetEditorIDStatus::Changed, "unbounded editor ID should be stored");
		const auto* editorID = RequireNotNull(LookupTable::FindEditorID(0x3333u, 0x01u), "unbounded editor ID should be stored");

		RequireEqual(*editorID, std::string("abcdef"), "editor ID should remain unbounded");
	}

	void TestSetterReportsPreviousValueWhenReplacingSameForm()
	{
		LookupTable::Reset({});

		const auto first = LookupTable::SetEditorID(0x3333u, 0x01u, "Alpha");
		RequireEqual(first.status, LookupTable::SetEditorIDStatus::Changed, "first editor ID should be stored");
		Require(!first.previousEditorID, "first write should not report a previous editor ID");
		const auto* firstCurrentEditorID = RequireNotNull(first.currentEditorID, "first write should report the current editor ID");
		RequireEqual(*firstCurrentEditorID, std::string("Alpha"), "first write should report Alpha as current");

		const auto second = LookupTable::SetEditorID(0x3333u, 0x01u, "Beta");
		RequireEqual(second.status, LookupTable::SetEditorIDStatus::Changed, "replacement editor ID should be stored");
		const auto* secondPreviousEditorID = RequireNotNull(second.previousEditorID, "replacement write should report the previous editor ID");
		const auto* secondCurrentEditorID = RequireNotNull(second.currentEditorID, "replacement write should report the current editor ID");
		RequireEqual(*secondPreviousEditorID, std::string("Alpha"), "replacement write should report Alpha as previous");
		RequireEqual(*secondCurrentEditorID, std::string("Beta"), "replacement write should report Beta as current");

		const auto* editorID = RequireNotNull(LookupTable::FindEditorID(0x3333u, 0x01u), "replacement editor ID should remain stored");
		RequireEqual(*editorID, std::string("Beta"), "stored editor ID should be replaced with Beta");
		RequireEqual(std::string_view(NEIF_GetEditorID({ 0x3333u, 0x01u })), "Beta"sv, "exported editor ID should reflect replacement");
	}

	void TestSetterSuppressesUnchangedEditorIDUpdates()
	{
		LookupTable::Reset({});
		RequireEqual(LookupTable::SetEditorID(0x3A3Au, 0x01u, "Alpha").status, LookupTable::SetEditorIDStatus::Changed, "baseline editor ID should be stored");

		bool publisherCalled = false;
		const auto result = LookupTable::Mutation::SetEditorID(
			0x3A3Au,
			0x01u,
			"Alpha",
			[&](LookupTable::EditorID, LookupTable::EditorID) {
				publisherCalled = true;
			});

		RequireEqual(result.status, LookupTable::SetEditorIDStatus::Unchanged, "unchanged editor ID should report an unchanged update");
		Require(!publisherCalled, "unchanged editor ID should not run the native publisher");
		const auto* previousEditorID = RequireNotNull(result.previousEditorID, "unchanged editor ID should report the existing previous value");
		const auto* currentEditorID = RequireNotNull(result.currentEditorID, "unchanged editor ID should report the existing current value");
		RequireEqual(*previousEditorID, std::string("Alpha"), "unchanged previous editor ID should remain Alpha");
		RequireEqual(*currentEditorID, std::string("Alpha"), "unchanged current editor ID should remain Alpha");
		RequireEqual(std::string_view(NEIF_GetEditorID({ 0x3A3Au, 0x01u })), "Alpha"sv, "unchanged lookup should remain visible");
	}

	void TestSetterIsIgnoredWhenLookupIsDisabled()
	{
		Settings::Values settings{};
		settings.enableLookupTable = false;
		LookupTable::Reset({ settings.enableLookupTable, settings.maxEditorIDLength });

		RequireEqual(LookupTable::SetEditorID(0x4444u, 0x01u, "Disabled").status, LookupTable::SetEditorIDStatus::Ignored, "lookup-disabled state should reject setter");
		Require(!LookupTable::FindEditorID(0x4444u, 0x01u), "lookup-disabled state should ignore setter");
	}

	void TestInternalSetEditorIDReturnsPreviousAndCurrentValues()
	{
		LookupTable::Reset({});
		RequireEqual(LookupTable::SetEditorID(0x7777u, 0x01u, "Old").status, LookupTable::SetEditorIDStatus::Changed, "baseline editor ID should be stored");

		bool publisherCalled = false;
		const auto result = LookupTable::Mutation::SetEditorID(
			0x7777u,
			0x01u,
			"New",
			[&](LookupTable::EditorID previousEditorID, LookupTable::EditorID currentEditorID) {
				publisherCalled = true;
				const auto* checkedPreviousEditorID = RequireNotNull(previousEditorID, "publisher should receive the previous editor ID");
				const auto* checkedCurrentEditorID = RequireNotNull(currentEditorID, "publisher should receive the current editor ID");
				RequireEqual(*checkedPreviousEditorID, std::string("Old"), "publisher should observe Old as previous");
				RequireEqual(*checkedCurrentEditorID, std::string("New"), "publisher should observe New as current");
			});

		RequireEqual(result.status, LookupTable::SetEditorIDStatus::Changed, "published editor ID should report success");
		Require(publisherCalled, "publisher should run for a stored editor ID");
		const auto* previousEditorID = RequireNotNull(result.previousEditorID, "published editor ID should expose the previous value");
		const auto* currentEditorID = RequireNotNull(result.currentEditorID, "published editor ID should expose the current value");
		RequireEqual(*previousEditorID, std::string("Old"), "published editor ID should preserve the previous editor ID");
		RequireEqual(*currentEditorID, std::string("New"), "published editor ID should expose the new editor ID");
		RequireEqual(std::string_view(NEIF_GetEditorID({ 0x7777u, 0x01u })), "New"sv, "published editor ID should be visible after success");
	}

	void TestInternalSetEditorIDRollsBackStateWhenPublisherFails()
	{
		LookupTable::Reset({});
		bool publisherCalled = false;

		RequireThrows<std::runtime_error>(
			[&] {
				(void)LookupTable::Mutation::SetEditorID(
					0x7878u,
					0x01u,
					"New",
					[&](LookupTable::EditorID, LookupTable::EditorID) {
						publisherCalled = true;
						throw std::runtime_error("publisher failed");
					});
			},
			"failed publisher should propagate");

		Require(publisherCalled, "publisher should run before failure");
		Require(!LookupTable::FindEditorID(0x7878u, 0x01u), "failed publication should roll back reserved state");
		RequireEqual(std::string_view(NEIF_GetEditorID({ 0x7878u, 0x01u })), ""sv, "failed publication should not expose a new editor ID");
	}

	void TestSetEditorIDSerializesConcurrentSameKeyWrites()
	{
		LookupTable::Reset({});

		std::promise<void> firstPublisherEnteredPromise;
		auto firstPublisherEntered = firstPublisherEnteredPromise.get_future();
		std::promise<void> releaseFirstPublisherPromise;
		auto releaseFirstPublisher = releaseFirstPublisherPromise.get_future().share();
		std::promise<void> secondPublisherEnteredPromise;
		auto secondPublisherEntered = secondPublisherEnteredPromise.get_future();

		std::thread firstWriter([&] {
			(void)LookupTable::Mutation::SetEditorID(
				0x7979u,
				0x01u,
				"First",
				[&](LookupTable::EditorID, LookupTable::EditorID) {
					firstPublisherEnteredPromise.set_value();
					releaseFirstPublisher.wait();
				});
		});

		firstPublisherEntered.wait();

		std::thread secondWriter([&] {
			(void)LookupTable::Mutation::SetEditorID(
				0x7979u,
				0x01u,
				"Second",
				[&](LookupTable::EditorID, LookupTable::EditorID) {
					secondPublisherEnteredPromise.set_value();
				});
		});

		RequireEqual(
			secondPublisherEntered.wait_for(100ms),
			std::future_status::timeout,
			"concurrent same-key publication should not enter the second publisher before the first finishes");

		releaseFirstPublisherPromise.set_value();
		firstWriter.join();
		secondWriter.join();

		RequireEqual(std::string_view(NEIF_GetEditorID({ 0x7979u, 0x01u })), "Second"sv, "second writer should win after serialized publication");
	}

	void TestSetEditorIDSerializesPublicAndHookOwnedWrites()
	{
		LookupTable::Reset({});

		std::promise<void> firstPublisherEnteredPromise;
		auto firstPublisherEntered = firstPublisherEnteredPromise.get_future();
		std::promise<void> releaseFirstPublisherPromise;
		auto releaseFirstPublisher = releaseFirstPublisherPromise.get_future().share();

		std::thread firstWriter([&] {
			(void)LookupTable::Mutation::SetEditorID(
				0x7A7Au,
				0x01u,
				"First",
				[&](LookupTable::EditorID, LookupTable::EditorID) {
					firstPublisherEnteredPromise.set_value();
					releaseFirstPublisher.wait();
				});
		});

		firstPublisherEntered.wait();

		auto secondWriter = std::async(
			std::launch::async,
			[] {
				return LookupTable::SetEditorID(0x7A7Au, 0x01u, "Second");
			});

		RequireEqual(
			secondWriter.wait_for(100ms),
			std::future_status::timeout,
			"public setter should not bypass the serialized publication window");

		releaseFirstPublisherPromise.set_value();
		firstWriter.join();

		const auto secondResult = secondWriter.get();
		RequireEqual(secondResult.status, LookupTable::SetEditorIDStatus::Changed, "public setter should still succeed after the first publication completes");
		const auto* previousEditorID = RequireNotNull(secondResult.previousEditorID, "public setter should observe the first writer as previous");
		RequireEqual(*previousEditorID, std::string("First"), "public setter should serialize behind the first writer");
		RequireEqual(std::string_view(NEIF_GetEditorID({ 0x7A7Au, 0x01u })), "Second"sv, "second writer should win after serialization");
	}

	void TestExportedLookupRemainsValidAcrossServiceMutation()
	{
		LookupTable::Reset({});
		RequireEqual(LookupTable::SetEditorID(0x5555u, 0x01u, "Alpha").status, LookupTable::SetEditorIDStatus::Changed, "first exported editor ID should be stored");

		const char* exported = NEIF_GetEditorID({ 0x5555u, 0x01u });
		RequireEqual(std::string_view(exported), "Alpha"sv, "exported editor ID should resolve");

		RequireEqual(LookupTable::SetEditorID(0x6666u, 0x01u, "Beta").status, LookupTable::SetEditorIDStatus::Changed, "second exported editor ID should be stored");
		RequireEqual(std::string_view(exported), "Alpha"sv, "exported editor ID pointer should stay valid after mutation");
	}

	void TestExportedLookupRemainsValidAcrossSameKeyReplacement()
	{
		LookupTable::Reset({});
		RequireEqual(LookupTable::SetEditorID(0x5656u, 0x01u, "Alpha").status, LookupTable::SetEditorIDStatus::Changed, "first exported editor ID should be stored");

		const char* exported = NEIF_GetEditorID({ 0x5656u, 0x01u });
		RequireEqual(std::string_view(exported), "Alpha"sv, "exported editor ID should resolve");

		RequireEqual(LookupTable::SetEditorID(0x5656u, 0x01u, "Beta").status, LookupTable::SetEditorIDStatus::Changed, "replacement editor ID should be stored");
		RequireEqual(std::string_view(exported), "Alpha"sv, "previous exported editor ID pointer should stay valid after same-key replacement");
		RequireEqual(std::string_view(NEIF_GetEditorID({ 0x5656u, 0x01u })), "Beta"sv, "new lookup should resolve the replacement value");
	}

	int RunConsumerImportAfterLoadScenario()
	{
		Require(
			GetModuleHandleW(L"NativeEditorIDFix") == nullptr,
			"fresh consumer process should not have NativeEditorIDFix.dll loaded yet");

		const auto module = LoadPluginModule();

		const auto stateExportAfterLoad = NEIF::Details::ExplicitImport<NEIF::Details::GetEditorIDLookupStateT>(
			NEIF::Details::GetEditorIDLookupStateName);
		const auto editorIDExportAfterLoad =
			NEIF::Details::ExplicitImport<NEIF::Details::GetEditorIDT>(NEIF::Details::GetEditorIDName);

		const auto checkedStateExportAfterLoad =
			RequireNotNull(stateExportAfterLoad, "consumer lookup-state import should resolve from NativeEditorIDFix.dll");
		const auto checkedEditorIDExportAfterLoad =
			RequireNotNull(editorIDExportAfterLoad, "consumer editor ID import should resolve from NativeEditorIDFix.dll");

		NEIF::TESForm form{};
		form.FormID = 0x7777u;
		form.FormType = 0x01u;

		RequireEqual(
			NEIF::GetEditorIDLookupState(),
			checkedStateExportAfterLoad(),
			"consumer wrapper should match direct exported lookup state after load");
		RequireEqual(
			std::string_view(NEIF::GetEditorID(&form)),
			std::string_view(checkedEditorIDExportAfterLoad(NEIF::FormInfo(form))),
			"consumer wrapper should match direct exported editor ID after load");

		FreeLibrary(module);
		return EXIT_SUCCESS;
	}

	int RunConsumerImportRecoversAfterPreloadMissScenario()
	{
		Require(
			GetModuleHandleW(L"NativeEditorIDFix") == nullptr,
			"fresh consumer process should not have NativeEditorIDFix.dll loaded yet");

		NEIF::TESForm form{};
		form.FormID = 0x8888u;
		form.FormType = 0x01u;

		RequireEqual(
			NEIF::GetEditorIDLookupState(),
			NEIF::EditorIDLookupState::None,
			"wrapper should return fallback state before the module is loaded");
		RequireEqual(
			std::string_view(NEIF::GetEditorID(&form)),
			""sv,
			"wrapper should return fallback editor ID before the module is loaded");

		const auto module = LoadPluginModule();
		const auto stateExportAfterLoad = NEIF::Details::ExplicitImport<NEIF::Details::GetEditorIDLookupStateT>(
			NEIF::Details::GetEditorIDLookupStateName);
		const auto editorIDExportAfterLoad =
			NEIF::Details::ExplicitImport<NEIF::Details::GetEditorIDT>(NEIF::Details::GetEditorIDName);

		const auto checkedStateExportAfterLoad =
			RequireNotNull(stateExportAfterLoad, "explicit import should resolve after NativeEditorIDFix.dll is loaded");
		const auto checkedEditorIDExportAfterLoad =
			RequireNotNull(editorIDExportAfterLoad, "explicit editor ID import should resolve after NativeEditorIDFix.dll is loaded");
		RequireEqual(
			NEIF::GetEditorIDLookupState(),
			checkedStateExportAfterLoad(),
			"wrapper should recover after an early pre-load call once the module is loaded");
		RequireEqual(
			std::string_view(NEIF::GetEditorID(&form)),
			std::string_view(checkedEditorIDExportAfterLoad(NEIF::FormInfo(form))),
			"editor ID wrapper should recover after an early pre-load call once the module is loaded");

		FreeLibrary(module);
		return EXIT_SUCCESS;
	}

	void TestConsumerImportPathResolvesInFreshProcess()
	{
		RequireEqual(
			RunChildScenario(L"--child-consumer-import-after-load"),
			static_cast<DWORD>(EXIT_SUCCESS),
			"fresh-process consumer import scenario should succeed");
	}

	void TestConsumerImportRecoversAfterPreloadMiss()
	{
		RequireEqual(
			RunChildScenario(L"--child-consumer-import-cached-miss"),
			static_cast<DWORD>(EXIT_SUCCESS),
			"pre-load consumer miss should recover once the module loads");
	}
}

int wmain(int argc, wchar_t** argv)
{
	if (argc == 2) {
		const std::wstring_view mode(argv[1]);
		if (mode == L"--child-consumer-import-after-load") {
			return RunConsumerImportAfterLoadScenario();
		}
		if (mode == L"--child-consumer-import-cached-miss") {
			return RunConsumerImportRecoversAfterPreloadMissScenario();
		}
	}

	constexpr std::array tests{
		TestCase{ "SettingsMissingFileUsesDefaults", &TestSettingsMissingFileUsesDefaults },
		TestCase{ "SettingsMissingKeysPreserveDefaults", &TestSettingsMissingKeysPreserveDefaults },
		TestCase{ "SettingsMalformedValuesFallBackPerKey", &TestSettingsMalformedValuesFallBackPerKey },
		TestCase{ "SettingsDebugTraceFlagsParseAndFallBack", &TestSettingsDebugTraceFlagsParseAndFallBack },
		TestCase{ "ConfiguredLookupStateMatrix", &TestConfiguredLookupStateMatrix },
		TestCase{ "EffectiveLookupStateResolutionRequiresSatisfiedNativeContracts", &TestEffectiveLookupStateResolutionRequiresSatisfiedNativeContracts },
		TestCase{ "MissingLookupReturnsEmptyString", &TestMissingLookupReturnsEmptyString },
		TestCase{ "SetterIgnoresNullAndEmpty", &TestSetterIgnoresNullAndEmpty },
		TestCase{ "SetterIgnoresStoryManagerNodes", &TestSetterIgnoresStoryManagerNodes },
		TestCase{ "SetterRespectsMaxEditorIDLength", &TestSetterRespectsMaxEditorIDLength },
		TestCase{ "SetterAllowsUnlimitedEditorIDLength", &TestSetterAllowsUnlimitedEditorIDLength },
		TestCase{ "SetterReportsPreviousValueWhenReplacingSameForm", &TestSetterReportsPreviousValueWhenReplacingSameForm },
		TestCase{ "SetterSuppressesUnchangedEditorIDUpdates", &TestSetterSuppressesUnchangedEditorIDUpdates },
		TestCase{ "SetterIsIgnoredWhenLookupIsDisabled", &TestSetterIsIgnoredWhenLookupIsDisabled },
		TestCase{ "InternalSetEditorIDReturnsPreviousAndCurrentValues", &TestInternalSetEditorIDReturnsPreviousAndCurrentValues },
		TestCase{ "InternalSetEditorIDRollsBackStateWhenPublisherFails", &TestInternalSetEditorIDRollsBackStateWhenPublisherFails },
		TestCase{ "SetEditorIDSerializesConcurrentSameKeyWrites", &TestSetEditorIDSerializesConcurrentSameKeyWrites },
		TestCase{ "SetEditorIDSerializesPublicAndHookOwnedWrites", &TestSetEditorIDSerializesPublicAndHookOwnedWrites },
		TestCase{ "ExportedLookupRemainsValidAcrossServiceMutation", &TestExportedLookupRemainsValidAcrossServiceMutation },
		TestCase{ "ExportedLookupRemainsValidAcrossSameKeyReplacement", &TestExportedLookupRemainsValidAcrossSameKeyReplacement },
		TestCase{ "ConsumerImportPathResolvesInFreshProcess", &TestConsumerImportPathResolvesInFreshProcess },
		TestCase{ "ConsumerImportRecoversAfterPreloadMiss", &TestConsumerImportRecoversAfterPreloadMiss }
	};

	std::size_t failures = 0;
	for (const auto& test : tests) {
		try {
			test.run();
		} catch (const std::exception& exception) {
			std::cerr << "[FAIL] " << test.name << ": " << exception.what() << '\n';
			++failures;
		}
	}

	if (failures != 0) {
		std::cerr << failures << " test(s) failed.\n";
		return EXIT_FAILURE;
	}

	std::cout << "All " << tests.size() << " tests passed.\n";
	return EXIT_SUCCESS;
}
