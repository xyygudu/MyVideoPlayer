## Purpose

Defines the MediaFrame class, MediaType enum, and associated PixelFormat/
SampleFormat enums used throughout the media pipeline.

## Requirements

### Requirement: MediaFrame 作为管线传输的帧类型
`MediaFrame` SHALL 是**纯数据载体**：持有 `AVFrame` 的所有权（RAII、move-only），并提供像素/采样访问（`width` / `height` / `format` / `PlaneData` / `PlaneLinesize` / `RawFrame` / `IsValid` / `MakeWritable`）。作为 `MediaBuffer` 的 variant 成员之一在 Link 中传输。

`MediaFrame` SHALL NOT 持有时间戳或媒体类型 —— 二者是传输层元数据，由 `MediaBuffer` 唯一承载。同一事实 SHALL NOT 在两层各存一份。

`AVFrame::pts` SHALL 仅在进出 FFmpeg 的边界上被视为有效（解码输出、编码输入）。管线中段 SHALL 以 `MediaBuffer::timestamp()` 为准 —— 由帧池新建的中间帧其 `AVFrame::pts` 为 `AV_NOPTS_VALUE`。

#### Scenario: 裸帧接口不需要时间
- **WHEN** 渲染器、像素运算、格式转换接收一个 `MediaFrame`
- **THEN** 它们只访问像素数据与尺寸/格式，不需要也无法从帧上取得 pts

#### Scenario: 一帧产出多帧时各自带时间
- **WHEN** 某变换节点从 1 个输入帧产出 N 个输出帧（如反交错输出两场）
- **THEN** 节点构造 N 个 `MediaBuffer`，各自携带独立的 `Timestamp`，无需在帧上维护第二份时间

#### Scenario: 中间帧的 AVFrame pts 不可信
- **WHEN** 读取效果节点输出帧的 `RawFrame()->pts`
- **THEN** 其值为 `AV_NOPTS_VALUE`，调用方 SHALL 改用所属 `MediaBuffer` 的 timestamp

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
