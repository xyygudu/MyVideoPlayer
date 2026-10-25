#ifndef MVP_LOGGING_H_
#define MVP_LOGGING_H_

#include <string>

#include "mvp/export.h"

namespace mvp {
namespace logging {

enum class Level { kTrace, kDebug, kInfo, kWarn, kError };

MVP_CORE_EXPORT void Init();
// Console output is on by default; can be toggled at any time after Init().
MVP_CORE_EXPORT void SetConsoleLoggingEnabled(bool enabled);
MVP_CORE_EXPORT bool IsConsoleLoggingEnabled();
// Adds a file sink; returns false (and keeps existing sinks) if the file cannot be opened.
MVP_CORE_EXPORT bool EnableFileLogging(const std::string& path);
// The file EnableFileLogging opened; empty when logging to file is off.
MVP_CORE_EXPORT std::string FileLogPath();
// Applies to our own messages and to FFmpeg's (bridged) diagnostics.
MVP_CORE_EXPORT void SetLevel(Level level);
MVP_CORE_EXPORT Level GetLevel();

}  // namespace logging
}  // namespace mvp

#endif  // MVP_LOGGING_H_
