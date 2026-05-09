#include "lookup/LookupTable.h"

#include "lookup/LookupTableMutation.h"
#include "pch.h"

#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

namespace
{
	constexpr std::uint8_t kStoryManagerNodeType = 0x72;

	struct LookupTableData
	{
		bool enabled = true;
		std::size_t maxEditorIDLength = 128;
		std::unordered_map<std::uint64_t, LookupTable::EditorID> entries;
		std::vector<std::unique_ptr<const std::string>> editorIDStorage;
		mutable std::shared_mutex mutex;
	};

	LookupTableData g_table;
	std::mutex g_publishMutex;

	std::uint64_t MakeKey(std::uint32_t formID, std::uint8_t formType)
	{
		return (static_cast<std::uint64_t>(formType) << 32) | formID;
	}

	std::string NormalizeEditorID(std::size_t maxEditorIDLength, const char* editorID)
	{
		if (editorID == nullptr || editorID[0] == '\0') {
			return {};
		}

		if (maxEditorIDLength == 0) {
			return std::string(editorID);
		}

		const auto length = strnlen_s(editorID, maxEditorIDLength);
		return std::string(editorID, length);
	}

	enum class EditorIDUpdateKind
	{
		Ignored,
		Unchanged,
		Changed
	};

	struct PreparedEditorIDUpdate
	{
		EditorIDUpdateKind kind = EditorIDUpdateKind::Ignored;
		std::uint64_t key = 0;
		LookupTable::EditorID previousEditorID = nullptr;
		LookupTable::EditorID currentEditorID = nullptr;
		std::unique_ptr<const std::string> pendingEditorID;
	};

	[[nodiscard]] PreparedEditorIDUpdate PrepareEditorIDUpdate(
		std::uint32_t formID,
		std::uint8_t formType,
		const char* editorID)
	{
		std::unique_lock lock(g_table.mutex);

		if (!g_table.enabled || formType == kStoryManagerNodeType) {
			return {};
		}

		auto normalized = NormalizeEditorID(g_table.maxEditorIDLength, editorID);
		if (normalized.empty()) {
			return {};
		}

		PreparedEditorIDUpdate prepared{};
		prepared.key = MakeKey(formID, formType);

		if (const auto existingEntry = g_table.entries.find(prepared.key); existingEntry != g_table.entries.end()) {
			prepared.previousEditorID = existingEntry->second;
			if (prepared.previousEditorID != nullptr && *prepared.previousEditorID == normalized) {
				prepared.kind = EditorIDUpdateKind::Unchanged;
				prepared.currentEditorID = prepared.previousEditorID;
				return prepared;
			}
		}

		prepared.pendingEditorID = std::make_unique<const std::string>(std::move(normalized));
		prepared.currentEditorID = prepared.pendingEditorID.get();
		prepared.kind = EditorIDUpdateKind::Changed;
		return prepared;
	}

	void CommitPreparedEditorIDUpdate(PreparedEditorIDUpdate& prepared)
	{
		if (prepared.kind != EditorIDUpdateKind::Changed) {
			return;
		}
		if (!prepared.pendingEditorID || prepared.currentEditorID == nullptr) {
			throw std::logic_error("prepared editor ID value was missing during commit");
		}

		std::unique_lock lock(g_table.mutex);
		const auto* currentEditorID = prepared.currentEditorID;
		g_table.editorIDStorage.push_back(std::move(prepared.pendingEditorID));
		g_table.entries[prepared.key] = currentEditorID;
	}

	[[nodiscard]] LookupTable::SetEditorIDResult SetEditorIDImpl(
		std::uint32_t formID,
		std::uint8_t formType,
		const char* editorID,
		const LookupTable::Mutation::EditorIDPublisher& publisher)
	{
		std::lock_guard publishLock(g_publishMutex);

		auto prepared = PrepareEditorIDUpdate(formID, formType, editorID);
		if (prepared.kind == EditorIDUpdateKind::Ignored) {
			return {};
		}

		if (prepared.kind == EditorIDUpdateKind::Changed) {
			if (publisher) {
				publisher(prepared.previousEditorID, prepared.currentEditorID);
			}
			CommitPreparedEditorIDUpdate(prepared);
		}

		const auto status = prepared.kind == EditorIDUpdateKind::Unchanged ?
		                        LookupTable::SetEditorIDStatus::Unchanged :
		                        LookupTable::SetEditorIDStatus::Changed;
		return LookupTable::SetEditorIDResult{
			status,
			prepared.previousEditorID,
			prepared.currentEditorID
		};
	}
}

namespace LookupTable
{
	void Reset(Options options)
	{
		std::lock_guard publishLock(g_publishMutex);
		std::unique_lock lock(g_table.mutex);

		g_table.enabled = options.enabled;
		g_table.maxEditorIDLength = options.maxEditorIDLength;
		g_table.entries.clear();
		g_table.editorIDStorage.clear();
	}

	EditorID FindEditorID(std::uint32_t formID, std::uint8_t formType)
	{
		std::shared_lock lock(g_table.mutex);
		const auto it = g_table.entries.find(MakeKey(formID, formType));
		return it != g_table.entries.end() ? it->second : nullptr;
	}

	SetEditorIDResult SetEditorID(std::uint32_t formID, std::uint8_t formType, const char* editorID)
	{
		return SetEditorIDImpl(formID, formType, editorID, {});
	}
}

namespace LookupTable::Mutation
{
	SetEditorIDResult SetEditorID(
		std::uint32_t formID,
		std::uint8_t formType,
		const char* editorID,
		const EditorIDPublisher& publisher)
	{
		return SetEditorIDImpl(formID, formType, editorID, publisher);
	}
}
