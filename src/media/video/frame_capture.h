#ifndef MVP_VIDEO_FRAME_CAPTURE_H_
#define MVP_VIDEO_FRAME_CAPTURE_H_

#include <cstdint>
#include <vector>

#include "base/media_frame.h"

namespace mvp {

/// Converts a frame (software or hardware) to tightly packed 8-bit RGBA,
/// using the frame's own YUV matrix and range. False (logged) when the frame
/// cannot be converted.
bool ConvertToRgba(const MediaFrame& frame, std::vector<uint8_t>* rgba);

}  // namespace mvp

#endif  // MVP_VIDEO_FRAME_CAPTURE_H_
