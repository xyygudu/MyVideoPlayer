#ifndef MVP_LOGGING_H_
#define MVP_LOGGING_H_

#include <string>

#include "mvp/export.h"

namespace mvp {
namespace logging {

MVP_CORE_EXPORT void Init();
// Console output is on by default; can be toggled at any time after Init().
MVP_CORE_EXPORT void SetConsoleLoggingEnabled(bool enabled);
// Adds a file sink; returns false (and keeps existing sinks) if the file cannot be opened.
MVP_CORE_EXPORT bool EnableFileLogging(const std::string& path);

}  // namespace logging
}  // namespace mvp

#endif  // MVP_LOGGING_H_
