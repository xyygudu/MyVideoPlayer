#include "mvp/logging.h"

#include <atomic>
#include <cstdarg>
#include <memory>
#include <string>

extern "C" {
#include <libavutil/log.h>
}
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace mvp {
namespace logging {

namespace {

constexpr const char* kPattern = "[%Y-%m-%d %H:%M:%S.%e] [%l] [t%t] [%s:%#] %v";

spdlog::level::level_enum MapFfmpegLevel(int level) {
    if (level <= AV_LOG_FATAL) return spdlog::level::critical;
    if (level <= AV_LOG_ERROR) return spdlog::level::err;
    if (level <= AV_LOG_WARNING) return spdlog::level::warn;
    if (level <= AV_LOG_INFO) return spdlog::level::info;
    if (level <= AV_LOG_VERBOSE) return spdlog::level::debug;
    return spdlog::level::trace;
}

// FFmpeg may emit one logical line across several calls; buffer per thread
// until the terminating newline.
void FfmpegLogCallback(void* avcl, int level, const char* fmt, va_list vl) {
    if (level > av_log_get_level()) {
        return;
    }
    thread_local int print_prefix = 1;
    thread_local std::string pending;
    char chunk[1024];
    av_log_format_line2(avcl, level, fmt, vl, chunk, sizeof(chunk),
                        &print_prefix);
    pending += chunk;
    if (pending.empty() || pending.back() != '\n') {
        return;
    }
    pending.pop_back();
    if (!pending.empty()) {
        spdlog::log(MapFfmpegLevel(level), "[ffmpeg] {}", pending);
    }
    pending.clear();
}

}  // namespace

static std::atomic<bool> g_initialized{false};
static std::shared_ptr<spdlog::sinks::sink> g_console_sink;

void Init() {
    if (g_initialized.exchange(true)) {
        return;  // Already initialized
    }

    g_console_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>("mvp", g_console_sink);
    logger->set_pattern(kPattern);
    logger->set_level(spdlog::level::debug);
    spdlog::set_default_logger(logger);

    // FFmpeg's own diagnostics (hwaccel/surface-pool errors etc.) otherwise
    // go to stderr only and never reach the log file.
    av_log_set_callback(&FfmpegLogCallback);
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
        file_sink->set_pattern(kPattern);
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
