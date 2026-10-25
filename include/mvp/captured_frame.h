#ifndef MVP_CAPTURED_FRAME_H_
#define MVP_CAPTURED_FRAME_H_

#include <cstdint>
#include <vector>

namespace mvp {

/// A video frame converted to 8-bit RGBA, e.g. for screenshots and previews.
struct CapturedFrame {
    int width{0};               // 0 = no frame
    int height{0};
    double pts{0.0};            // Presentation time in seconds
    std::vector<uint8_t> rgba;  // width * height * 4 bytes, rows tightly packed
};

}  // namespace mvp

#endif  // MVP_CAPTURED_FRAME_H_
