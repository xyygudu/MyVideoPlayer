#include "mvp/logging.h"

#include <atomic>
#include <memory>
#include <string>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace mvp {
namespace logging {

static std::atomic<bool> g_initialized{false};
static std::shared_ptr<spdlog::sinks::sink> g_console_sink;

void Init() {
    if (g_initialized.exchange(true)) {
        return;  // Already initialized
    }

    g_console_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("mvp", g_console_sink);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v");
    logger->set_level(spdlog::level::debug);
    spdlog::set_default_logger(logger);
}

void SetConsoleLoggingEnabled(bool enabled) {
    if (g_console_sink) {
        g_console_sink->set_level(enabled ? spdlog::level::trace : spdlog::level::off);
    }
}

bool EnableFileLogging(const std::string& path) {
    auto logger = spdlog::default_logger();
    if (!logger) {
        return false;
    }

    try {
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path, true);
        logger->sinks().push_back(file_sink);
        // Flush every message so the file is readable even while the app is
        // hung/frozen (otherwise buffered logs are lost on exit).
        logger->flush_on(spdlog::level::trace);
        return true;
    } catch (const spdlog::spdlog_ex& ex) {
        SPDLOG_ERROR("Failed to enable file logging at '{}': {}", path, ex.what());
        return false;
    }
}

}  // namespace logging
}  // namespace mvp
