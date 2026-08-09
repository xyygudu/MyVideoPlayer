#pragma once

#include <string>

#include "mvp/container_caps.h"
#include "mvp/export.h"

namespace mvp {

/// Standalone container/codec compatibility probe.
///
/// Mirrors SourceProbe: independent of any Graph or Node, callable from UI
/// (populating an encoder dropdown) or from MuxNode::DeclareCaps() (caps
/// negotiation) without either side duplicating the FFmpeg query logic.
class MVP_CORE_EXPORT ContainerProbe {
  public:
    /// Query which encoders a container can store, plus its default.
    /// `container_or_path` accepts either a bare container name ("mp4",
    /// "mkv") or a full output path — both resolve via av_guess_format.
    /// Returns an empty ContainerCodecCaps if the container can't be
    /// resolved.
    static ContainerCodecCaps Query(const std::string& container_or_path);
};

}  // namespace mvp
