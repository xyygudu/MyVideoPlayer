## Purpose

Defines the MediaFrame class, MediaType enum, and associated PixelFormat/
SampleFormat enums used throughout the media pipeline.

## Requirements

### Requirement: MediaFrame 作为管线传输的帧类型
MediaFrame SHALL 持有 `AVFrame*`（RawFrame），提供 pts/IsValid/type/RawFrame 接口。作为 `MediaBuffer` 的 variant 成员之一在 Link 中传输。

### Requirement: MediaType 枚举
系统 SHALL 定义 `enum class MediaType { kUnknown, kAudio, kVideo, kSubtitle }`。

### Requirement: PixelFormat 和 SampleFormat 枚举
系统 SHALL 在 media_frame.h 中定义 `PixelFormat` 和 `SampleFormat` 枚举，供 media_format.h（VideoFormat/AudioFormat/FormatCaps）和 decoder_node.cc 使用。

```cpp
enum class PixelFormat {
    kUnknown = 0, kYUV420P, kYUV422P, kYUV444P, kNV12, kRGB32, kD3D11,
};
enum class SampleFormat {
    kUnknown = 0, kS16, kS32, kFloat, kS16Planar, kFloatPlanar,
};
```

#### Scenario: media_format.h 引用无额外依赖
- **WHEN** media_format.h 使用 PixelFormat/SampleFormat
- **THEN** 仅需 `#include "media_frame.h"`
