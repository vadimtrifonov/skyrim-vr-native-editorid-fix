#include "lookup/LookupMode.h"

#include "pch.h"

#include <atomic>
#include <stdexcept>

namespace
{
	std::atomic<std::uint32_t> g_effectiveMode{ static_cast<std::uint32_t>(LookupMode::Value::ExternalOnly) };
}

namespace LookupMode
{
	Value FromSettings(const Settings::Values& settings)
	{
		using enum Value;

		if (!settings.enableLookupTable) {
			return None;
		}
		if (!settings.enableNativeEditorIDLookup) {
			return ExternalOnly;
		}
		if (!settings.excludeEditorIDFromNodeNaming) {
			return Native;
		}

		return NativeNoNodes;
	}

	Value ResolveEffective(Value configuredMode, bool nativeGettersActive, bool nodeNamingCompatibilityActive)
	{
		using enum Value;

		if (configuredMode == None) {
			return None;
		}
		if (configuredMode == ExternalOnly) {
			return ExternalOnly;
		}

		if (configuredMode == NativeNoNodes && !nodeNamingCompatibilityActive) {
			throw std::runtime_error("Configured NativeNoNodes requires active node naming compatibility patches.");
		}
		if (!nativeGettersActive) {
			throw std::runtime_error("Configured native EditorID lookup requires active native getter hooks.");
		}

		return configuredMode;
	}

	void SetEffective(Value mode)
	{
		g_effectiveMode.store(static_cast<std::uint32_t>(mode), std::memory_order_release);
	}

	Value GetEffective()
	{
		return static_cast<Value>(g_effectiveMode.load(std::memory_order_acquire));
	}
}
