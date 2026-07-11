#pragma once

#include <string>

#include "mvp/source_info.h"

namespace mvp {

/// Standalone source probing utility.
///
/// Opens the file, reads container and stream metadata, then closes it.
/// Independent of any Graph or Node — can be used for pre-flight checks,
/// UI stream listing, or CanPlay(path) without constructing a pipeline.
class SourceProbe {
public:
    /// Probe a media file and return full SourceInfo.
    /// Returns an empty SourceInfo (no streams) on failure.
    static SourceInfo Probe(const std::string& filepath);
};

}  // namespace mvp
