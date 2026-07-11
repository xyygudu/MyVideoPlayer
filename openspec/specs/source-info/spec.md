## Purpose

Defines the SourceInfo, VideoStream, and AudioStream structures used to describe
media file container metadata and individual stream properties. Designed to be
reusable across UI display, stream selection, CanPlay() pre-flight checks, and
graph construction.

## Requirements

### Requirement: SourceInfo 通用结构体
系统 SHALL 定义 `SourceInfo` 结构体，描述媒体文件的容器级信息和所有音视频流属性。

```cpp
struct VideoStream {
    int index{-1};
    std::string codec_name;
    int width{0};
    int height{0};
    int frame_rate_num{0};
    int frame_rate_den{1};
    int pix_fmt{-1};
    int64_t bit_rate{0};
};

struct AudioStream {
    int index{-1};
    std::string codec_name;
    int sample_rate{0};
    int channels{0};
    uint64_t channel_layout{0};
    int sample_fmt{-1};
    int64_t bit_rate{0};
};

struct SourceInfo {
    std::string filepath;
    double duration{0.0};
    std::string format_name;
    int64_t bit_rate{0};
    std::vector<VideoStream> video_streams;
    std::vector<AudioStream> audio_streams;
};
```

SourceInfo SHALL 与 FFmpeg 类型解耦：`codec_name` 为字符串而非 `AVCodecID` 枚举，便于 UI 展示和跨模块传递。

#### Scenario: 含视频和音频的文件
- **WHEN** `SourceProbe::Probe("video_with_audio.mp4")` 被调用
- **THEN** 返回的 `SourceInfo::video_streams` 包含 1 个 VideoStream，含 index、codec_name、width、height、frame_rate
- **AND** `SourceInfo::audio_streams` 包含 1 个 AudioStream，含 index、codec_name、sample_rate、channels

#### Scenario: 仅含音频的文件
- **WHEN** `SourceProbe::Probe("audio_only.mp3")` 被调用
- **THEN** `video_streams` 为空，`audio_streams` 含 1 个 AudioStream
- **AND** `duration` 和 `format_name` 正确填充

#### Scenario: 含多视频流的文件
- **WHEN** 文件包含多个视频流（如多角度录制）
- **THEN** `video_streams.size() >= 2`，每个 VideoStream 的 index 唯一
