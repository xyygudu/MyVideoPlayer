#include "mvp/logging.h"

#include <atomic>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace mvp {
namespace logging {

static std::atomic<bool> g_initialized{false};

void Init() {
    if (g_initialized.exchange(true)) {
        return;  // Already initialized
    }

    auto console_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("mvp", console_sink);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v");
    logger->set_level(spdlog::level::debug);
    spdlog::set_default_logger(logger);
}

void EnableFileLogging(const std::string& path) {
    auto logger = spdlog::default_logger();
    if (!logger) {
        return;
    }

    try {
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path, true);
        logger->sinks().push_back(file_sink);
        // Flush every message so the file is readable even while the app is
        // hung/frozen (otherwise buffered logs are lost on exit).
        logger->flush_on(spdlog::level::trace);
    } catch (const spdlog::spdlog_ex& ex) {
        SPDLOG_ERROR("Failed to enable file logging at '{}': {}", path, ex.what());
    }
}

}  // namespace logging
}  // namespace mvp
