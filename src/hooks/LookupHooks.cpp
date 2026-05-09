#include "hooks/LookupHooks.h"

#include "pch.h"

#include "lookup/LookupTableMutation.h"

#include <array>
#include <cstring>
#include <optional>

namespace
{
	constexpr std::uint64_t kTESFormGetEditorIDSE = 10827;
	constexpr std::uint64_t kTESFormGetEditorIDAE = 10879;
	constexpr std::size_t kTESFormGetEditorIDVR = 0x107430;
	constexpr std::uint64_t kTESFormSetEditorIDSE = 10883;
	constexpr std::uint64_t kTESFormSetEditorIDAE = 10926;
	constexpr std::uint64_t kTESObjectREFRGetEditorIDSE = 19351;
	constexpr std::uint64_t kTESObjectREFRGetEditorIDAE = 19778;
	constexpr std::size_t kTESObjectREFRGetEditorIDVR = 0x2A7690;
	constexpr std::uint64_t kFormStringHashTableInsertSE = 13833;
	constexpr std::uint64_t kFormStringHashTableInsertAE = 13808;
	constexpr std::size_t kFormStringHashTableInsertVR = 0x1895A0;
	constexpr std::uint64_t kEditorIDHashTablePtrSE = 514352;
	constexpr std::uint64_t kEditorIDHashTablePtrAE = 400509;
	constexpr std::uint64_t kEditorIDHashTableLockSE = 514361;
	constexpr std::uint64_t kEditorIDHashTableLockAE = 400518;
	constexpr std::size_t kBranchHookTrampolineSize = 14;
	constexpr std::size_t kDirectHookPatchSize = 5;
	struct PreparedSetterHook
	{
		std::uintptr_t target = 0;
		std::array<std::uint8_t, 6> originalBytes{};
	};

	struct PreparedDirectHook
	{
		std::uintptr_t target = 0;
		std::array<std::uint8_t, kDirectHookPatchSize> originalBytes{};
		std::string_view label;
	};

	struct NativePublicationTargets
	{
		std::uintptr_t insertTarget = 0;
		std::uintptr_t tablePointerAddress = 0;
		std::uintptr_t lockAddress = 0;
	};

	struct TraceConfig
	{
		bool setFormEditorID = false;
		bool nativeEditorIDMap = false;
		bool getFormEditorID = false;
		bool getReferenceEditorID = false;
	};

	TraceConfig g_traceConfig{};

	void CacheTraceConfig(const Settings::Values& settings)
	{
		g_traceConfig = TraceConfig{
			settings.traceSetFormEditorID,
			settings.traceNativeEditorIDMap,
			settings.traceGetFormEditorID,
			settings.traceGetReferenceEditorID
		};
	}

	bool SetFormEditorID(RE::TESForm* form, const char* editorID);
	const char* GetFormEditorID(const RE::TESForm* form);
	const char* GetReferenceEditorID(const RE::TESObjectREFR* ref);

	template <class Callback>
	void BestEffortTrace(const Callback& callback) noexcept
	{
		try {
			callback();
		} catch (...) {
			// Trace logging must never alter hook behavior.
			return;
		}
	}

	[[nodiscard]] const char* SafeString(const char* value)
	{
		return value != nullptr ? value : "<null>";
	}

	[[nodiscard]] const char* SafeEditorID(LookupTable::EditorID editorID)
	{
		return editorID != nullptr ? editorID->c_str() : "";
	}

	[[nodiscard]] std::string_view ToString(LookupTable::SetEditorIDStatus status)
	{
		switch (status) {
		case LookupTable::SetEditorIDStatus::Ignored:
			return "Ignored";
		case LookupTable::SetEditorIDStatus::Unchanged:
			return "Unchanged";
		case LookupTable::SetEditorIDStatus::Changed:
			return "Changed";
		}

		return "Unknown";
	}

	[[nodiscard]] const void* AddressAsPointer(std::uintptr_t address) noexcept
	{
		return reinterpret_cast<const void*>(address);  // NOLINT(performance-no-int-to-ptr)
	}

	template <class T>
	[[nodiscard]] T ReadAddress(std::uintptr_t address)
	{
		T value{};
		std::memcpy(&value, AddressAsPointer(address), sizeof(value));
		return value;
	}

	[[nodiscard]] const char* GetSourceName(const RE::TESForm* form)
	{
		if (form == nullptr) {
			return "<null>";
		}

		const auto file = form->GetFile(0);
		return file != nullptr ? file->fileName : "<none>";
	}

	[[nodiscard]] std::uint32_t GetFormTypeValue(const RE::TESForm* form)
	{
		return form != nullptr ? static_cast<std::uint32_t>(form->GetFormType()) : 0;
	}

	[[nodiscard]] bool TraceSetFormEditorIDEnabled()
	{
		return g_traceConfig.setFormEditorID;
	}

	[[nodiscard]] bool TraceNativeEditorIDMapEnabled()
	{
		return g_traceConfig.nativeEditorIDMap;
	}

	[[nodiscard]] bool TraceGetFormEditorIDEnabled()
	{
		return g_traceConfig.getFormEditorID;
	}

	[[nodiscard]] bool TraceGetReferenceEditorIDEnabled()
	{
		return g_traceConfig.getReferenceEditorID;
	}

	[[nodiscard]] PreparedSetterHook PrepareSetterHook()
	{
		REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(kTESFormSetEditorIDSE, kTESFormSetEditorIDAE) };
		if (target.address() == 0) {
			throw std::runtime_error("Failed to resolve TESForm::SetFormEditorID hook target.");
		}

		PreparedSetterHook prepared{};
		prepared.target = target.address();
		std::memcpy(prepared.originalBytes.data(), AddressAsPointer(prepared.target), prepared.originalBytes.size());
		return prepared;
	}

	[[nodiscard]] PreparedDirectHook PrepareDirectHook(const REL::VariantID& id, std::string_view label)
	{
		REL::Relocation<std::uintptr_t> target{ id };
		if (target.address() == 0) {
			throw std::runtime_error(std::string("Failed to resolve ") + std::string(label) + " hook target.");
		}

		PreparedDirectHook prepared{};
		prepared.target = target.address();
		prepared.label = label;
		std::memcpy(prepared.originalBytes.data(), AddressAsPointer(prepared.target), prepared.originalBytes.size());
		return prepared;
	}

	[[nodiscard]] NativePublicationTargets PrepareNativePublicationTargets()
	{
		REL::Relocation<std::uintptr_t> insertTarget{ REL::VariantID(kFormStringHashTableInsertSE, kFormStringHashTableInsertAE, kFormStringHashTableInsertVR) };
		REL::Relocation<void**> tablePointer{ RELOCATION_ID(kEditorIDHashTablePtrSE, kEditorIDHashTablePtrAE) };
		REL::Relocation<RE::BSReadWriteLock*> lock{ RELOCATION_ID(kEditorIDHashTableLockSE, kEditorIDHashTableLockAE) };
		if (insertTarget.address() == 0) {
			throw std::runtime_error("Failed to resolve native EditorID hash insert target.");
		}
		if (tablePointer.address() == 0) {
			throw std::runtime_error("Failed to resolve native EditorID hash table pointer.");
		}
		if (lock.address() == 0) {
			throw std::runtime_error("Failed to resolve native EditorID hash lock.");
		}

		NativePublicationTargets prepared{};
		prepared.insertTarget = insertTarget.address();
		prepared.tablePointerAddress = tablePointer.address();
		prepared.lockAddress = lock.address();
		return prepared;
	}

	void ApplySetterHook(const PreparedSetterHook& prepared)
	{
		SKSE::GetTrampoline().write_branch<6>(prepared.target, SetFormEditorID);
		logs::info("Installed TESForm::SetFormEditorID hook at 0x{:X}", prepared.target);
	}

	void RollbackSetterHook(const PreparedSetterHook& prepared)
	{
		REL::safe_write(prepared.target, prepared.originalBytes.data(), prepared.originalBytes.size());
	}

	template <class F>
	void ApplyDirectHook(const PreparedDirectHook& prepared, F replacement)
	{
		SKSE::GetTrampoline().write_branch<kDirectHookPatchSize>(prepared.target, replacement);
		logs::info("Installed {} direct hook at 0x{:X}", prepared.label, prepared.target);
	}

	void RollbackDirectHook(const PreparedDirectHook& prepared)
	{
		REL::safe_write(prepared.target, prepared.originalBytes.data(), prepared.originalBytes.size());
	}

	[[nodiscard]] const char* LookupEditorID(std::uint32_t formID, std::uint8_t formType)
	{
		const auto* editorID = LookupTable::FindEditorID(formID, formType);
		return editorID != nullptr ? editorID->c_str() : "";
	}

	[[nodiscard]] const NativePublicationTargets& GetNativePublicationTargets()
	{
		static const NativePublicationTargets targets = PrepareNativePublicationTargets();
		return targets;
	}

	[[nodiscard]] void* GetNativeEditorIDHashTableBase(const NativePublicationTargets& targets)
	{
		return ReadAddress<void*>(targets.tablePointerAddress);
	}

	void PublishNativeEditorID(
		RE::TESForm* form,
		LookupTable::EditorID previousEditorID,
		LookupTable::EditorID currentEditorID,
		bool traceEnabled)
	{
		if (!currentEditorID) {
			if (traceEnabled) {
				BestEffortTrace([&] {
					logs::info(
						"[trace][publish] form {:08X} type={} source='{}' skipped because no stored EditorID is available",
						form->GetFormID(),
						GetFormTypeValue(form),
						GetSourceName(form));
				});
			}
			return;
		}

		const auto& targets = GetNativePublicationTargets();
		auto* const tableBase = GetNativeEditorIDHashTableBase(targets);
		if (tableBase == nullptr) {
			logs::critical("Native EditorID hash table was unavailable while publishing form {:08X}", form->GetFormID());
			SKSE::stl::report_and_fail("Native EditorID Fix VR expected the native EditorID hash table to be available.");
		}

		auto* const lock = reinterpret_cast<RE::BSReadWriteLock*>(targets.lockAddress);
		if (lock == nullptr) {
			logs::critical("Native EditorID hash lock was unavailable while publishing form {:08X}", form->GetFormID());
			SKSE::stl::report_and_fail("Native EditorID Fix VR expected the native EditorID hash lock to be available.");
		}

		bool published = false;
		{
			RE::BSWriteLockGuard writeLock{ *lock };
			if (REL::Module::IsAE()) {
				using NativeInsert = bool (*)(void*, const RE::BSFixedString&, const RE::TESForm*&, RE::TESForm*&);
				RE::BSFixedString editorID{ *currentEditorID };
				const RE::TESForm* formPointer = form;
				RE::TESForm* scratch = nullptr;
				auto* const tableBody = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(tableBase) + sizeof(void*));
				published = reinterpret_cast<NativeInsert>(targets.insertTarget)(tableBody, editorID, formPointer, scratch);
			} else {
				using NativeInsert = bool (*)(void*, const char*, const RE::TESForm&);
				published = reinterpret_cast<NativeInsert>(targets.insertTarget)(tableBase, currentEditorID->c_str(), *form);
			}
		}

		if (traceEnabled) {
			BestEffortTrace([&] {
				logs::info(
					"[trace][publish] form {:08X} type={} source='{}' old='{}' new='{}' published={} target=0x{:X}",
					form->GetFormID(),
					GetFormTypeValue(form),
					GetSourceName(form),
					SafeEditorID(previousEditorID),
					SafeEditorID(currentEditorID),
					published,
					targets.insertTarget);
			});
		}
	}

	bool SetFormEditorID(RE::TESForm* form, const char* editorID)
	{
		if (form == nullptr) {
			logs::critical("TESForm::SetFormEditorID hook received a null form pointer");
			return false;
		}

		const auto traceSetFormEditorID = TraceSetFormEditorIDEnabled();
		const auto traceNativeEditorIDMap = TraceNativeEditorIDMapEnabled();
		const auto setResult = LookupTable::Mutation::SetEditorID(
			form->GetFormID(),
			static_cast<std::uint8_t>(form->GetFormType()),
			editorID,
			[form, traceNativeEditorIDMap](LookupTable::EditorID previousEditorID, LookupTable::EditorID currentEditorID) {
				PublishNativeEditorID(form, previousEditorID, currentEditorID, traceNativeEditorIDMap);
			});

		if (traceSetFormEditorID) {
			logs::info(
				"[trace][set] form {:08X} type={} source='{}' incoming='{}' previous='{}' status={} current='{}'",
				form->GetFormID(),
				GetFormTypeValue(form),
				GetSourceName(form),
				SafeString(editorID),
				SafeEditorID(setResult.previousEditorID),
				ToString(setResult.status),
				SafeEditorID(setResult.currentEditorID));
		}

		// This hook owns EditorID lookup publication for the setter path.
		return true;
	}

	const char* GetFormEditorID(const RE::TESForm* form)
	{
		if (form == nullptr) {
			return "";
		}

		const auto editorID = LookupEditorID(form->GetFormID(), static_cast<std::uint8_t>(form->GetFormType()));
		if (TraceGetFormEditorIDEnabled()) {
			logs::info(
				"[trace][get-form] form {:08X} type={} source='{}' result='{}'",
				form->GetFormID(),
				GetFormTypeValue(form),
				GetSourceName(form),
				editorID);
		}

		return editorID;
	}

	const char* GetReferenceEditorID(const RE::TESObjectREFR* ref)
	{
		if (ref == nullptr) {
			return "";
		}

		const auto editorID = LookupEditorID(ref->GetFormID(), static_cast<std::uint8_t>(ref->GetFormType()));
		if (TraceGetReferenceEditorIDEnabled()) {
			logs::info(
				"[trace][get-ref] ref {:08X} type={} source='{}' result='{}'",
				ref->GetFormID(),
				GetFormTypeValue(ref),
				GetSourceName(ref),
				editorID);
		}

		return editorID;
	}
}

namespace Hooks::Lookup
{
	std::size_t GetTrampolineSize(LookupMode::Value mode)
	{
		if (mode == LookupMode::Value::None) {
			return 0;
		}

		std::size_t size = kBranchHookTrampolineSize;
		if (mode != LookupMode::Value::ExternalOnly) {
			size += (kBranchHookTrampolineSize * 2);
		}
		return size;
	}

	InstallResult Install(LookupMode::Value mode, const Settings::Values& settings)
	{
		InstallResult result{};
		CacheTraceConfig(settings);

		if (mode == LookupMode::Value::None) {
			return result;
		}

		static bool nativePublicationTargetsValidated = false;
		static bool setterInstalled = false;
		static bool tesFormGetterInstalled = false;
		static bool referenceGetterInstalled = false;
		std::optional<NativePublicationTargets> publicationTargetsToValidate;
		std::optional<PreparedSetterHook> setterToInstall;
		std::optional<PreparedDirectHook> tesFormGetterToInstall;
		std::optional<PreparedDirectHook> referenceGetterToInstall;

		if (!nativePublicationTargetsValidated) {
			publicationTargetsToValidate = PrepareNativePublicationTargets();
		}
		if (!setterInstalled) {
			setterToInstall = PrepareSetterHook();
		}
		if (mode != LookupMode::Value::ExternalOnly) {
			if (!tesFormGetterInstalled) {
				tesFormGetterToInstall = PrepareDirectHook(
					REL::VariantID(kTESFormGetEditorIDSE, kTESFormGetEditorIDAE, kTESFormGetEditorIDVR),
					"TESForm::GetFormEditorID");
			}
			if (!referenceGetterInstalled) {
				referenceGetterToInstall = PrepareDirectHook(
					REL::VariantID(kTESObjectREFRGetEditorIDSE, kTESObjectREFRGetEditorIDAE, kTESObjectREFRGetEditorIDVR),
					"TESObjectREFR::GetFormEditorID");
			}
		}

		bool publicationTargetsValidatedThisRun = false;
		bool setterApplied = false;
		bool tesFormGetterApplied = false;
		bool referenceGetterApplied = false;
		try {
			if (publicationTargetsToValidate) {
				logs::info(
					"Resolved native EditorID publication targets (insert=0x{:X}, table_ptr=0x{:X}, lock=0x{:X})",
					publicationTargetsToValidate->insertTarget,
					publicationTargetsToValidate->tablePointerAddress,
					publicationTargetsToValidate->lockAddress);
				nativePublicationTargetsValidated = true;
				publicationTargetsValidatedThisRun = true;
			}
			if (setterToInstall) {
				ApplySetterHook(*setterToInstall);
				setterInstalled = true;
				setterApplied = true;
			}
			if (tesFormGetterToInstall) {
				ApplyDirectHook(*tesFormGetterToInstall, &GetFormEditorID);
				tesFormGetterInstalled = true;
				tesFormGetterApplied = true;
			}
			if (referenceGetterToInstall) {
				ApplyDirectHook(*referenceGetterToInstall, &GetReferenceEditorID);
				referenceGetterInstalled = true;
				referenceGetterApplied = true;
			}
		} catch (...) {
			if (referenceGetterApplied) {
				RollbackDirectHook(*referenceGetterToInstall);
				referenceGetterInstalled = false;
			}
			if (tesFormGetterApplied) {
				RollbackDirectHook(*tesFormGetterToInstall);
				tesFormGetterInstalled = false;
			}
			if (setterApplied) {
				RollbackSetterHook(*setterToInstall);
				setterInstalled = false;
			}
			if (publicationTargetsValidatedThisRun) {
				nativePublicationTargetsValidated = false;
			}
			throw;
		}

		result.setterHookActive = setterInstalled;
		result.nativeGettersActive = tesFormGetterInstalled && referenceGetterInstalled;
		return result;
	}
}
