#pragma once

#include <string>
#include <vector>

namespace mvp {

/// Encoder support for a specific output container, decoupled from FFmpeg
/// types (encoder names, not AVCodecID) so it can be used directly by UI.
///
/// Populated from a curated candidate list (see ContainerProbe), not an
/// exhaustive enumeration of every codec FFmpeg knows about.
struct ContainerCodecCaps {
    std::vector<std::string> video_codecs;
    std::vector<std::string> audio_codecs;
    std::string default_video_codec;  // Empty = container has no usable default
    std::string default_audio_codec;
};

}  // namespace mvp
