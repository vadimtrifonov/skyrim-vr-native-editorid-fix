#include "bootstrap/Bootstrap.h"
#include "pch.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>

namespace
{
	std::filesystem::path InitializeLog()
	{
		auto path = logs::log_directory();
		if (!path) {
			SKSE::stl::report_and_fail("Failed to resolve the standard SKSE log directory.");
		}

		*path /= "NativeEditorIDFixVR.log";

		std::vector<spdlog::sink_ptr> sinks;
		sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true));

#ifndef NDEBUG
		sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
		const auto level = spdlog::level::trace;
#else
		const auto level = spdlog::level::info;
#endif

		auto logger = std::make_shared<spdlog::logger>("global log", sinks.begin(), sinks.end());
		logger->set_level(level);
		logger->flush_on(spdlog::level::info);

		spdlog::set_default_logger(std::move(logger));
		spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] [%s:%#] %v");

		return *path;
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	try {
		const auto logPath = InitializeLog();
		logs::info("===== Native EditorID Fix VR start =====");
		logs::info("Log file: {}", logPath.string());
		SKSE::Init(a_skse);
		Bootstrap::Initialize();

		logs::info("Native EditorID Fix VR loaded");
		return true;
	} catch (const std::exception& exception) {
		logs::critical("Native EditorID Fix VR failed to load: {}", exception.what());
	} catch (...) {
		logs::critical("Native EditorID Fix VR failed to load with an unknown exception");
	}

	return false;
}
