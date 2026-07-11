## ADDED Requirements

### Requirement: PixelFormat 和 SampleFormat 枚举定义在 media_frame.h
系统 SHALL 在 `src/media/media_frame.h` 中定义 `PixelFormat` 和 `SampleFormat` 枚举。

```cpp
enum class PixelFormat {
    kUnknown = 0, kYUV420P, kYUV422P, kYUV444P, kNV12, kRGB32, kD3D11,
};

enum class SampleFormat {
    kUnknown = 0, kS16, kS32, kFloat, kS16Planar, kFloatPlanar,
};
```

枚举 SHALL 与 FFmpeg 类型解耦，仅作为内部格式标签使用。FFmpeg 原始格式到枚举的映射 SHALL 在各消费方（video_renderer.cc）内部完成。

#### Scenario: media_format.h 引用无额外依赖
- **WHEN** media_format.h 中的 VideoFormat/AudioFormat/FormatCaps 使用 PixelFormat/SampleFormat
- **THEN** 仅需 `#include "media_frame.h"`（已存在的依赖）
