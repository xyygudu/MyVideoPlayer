#ifndef MVP_LOGGING_H_
#define MVP_LOGGING_H_

#include <string>

#include "mvp/export.h"

namespace mvp {
namespace logging {

MVP_CORE_EXPORT void Init();
MVP_CORE_EXPORT void EnableFileLogging(const std::string& path);

}  // namespace logging
}  // namespace mvp

#endif  // MVP_LOGGING_H_
