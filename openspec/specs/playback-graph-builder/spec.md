## Purpose

Defines the PlaybackGraphBuilder that encapsulates playback graph construction
logic with dependency injection via PlaybackContext. Supports chain-based
pipeline assembly and is filter-ready via FilterSpec.

## Requirements

### Requirement: PlaybackGraphBuilder 封装播放图构建
系统 SHALL 定义 `PlaybackGraphBuilder`，封装播放场景的图构建逻辑。

Builder SHALL 通过 `PlaybackContext` 结构体注入依赖：
```cpp
struct PlaybackContext {
    MediaGraph* graph;
    VideoRenderer* renderer;
    Clock* audio_clock;
    Clock* video_clock;
    void* window_handle;
    VideoFrameCallback video_cb;
    bool has_audio{false};
};
```

#### Scenario: Builder 用 Context 注入依赖
- **WHEN** 创建 PlaybackGraphBuilder
- **THEN** 通过单个 PlaybackContext 参数传入所有依赖

#### Scenario: BuildGraph 大幅瘦身
- **WHEN** MediaPlayer::BuildGraph 重构后
- **THEN** Probe → builder 逐流 AddPipeline → graph Negotiate/Prepare

### Requirement: 滤镜就绪的链式管线构建
PlaybackGraphBuilder SHALL 提供 `AddVideoPipeline(const StreamInfo&, const std::vector<FilterSpec>& filters = {})` 和 `AddAudioPipeline(...)`，将管线构建为线性链：`Decoder → [filters...] → Sink`。

```cpp
struct FilterSpec { std::string name; std::string args; };
```

#### Scenario: 无滤镜管线
- **WHEN** AddVideoPipeline(stream) 传空 filters
- **THEN** 构建 Decoder → VideoSink 链并连接

#### Scenario: 滤镜就绪（将来）
- **WHEN** 未来传非空 filters
- **THEN** 构建 Decoder → Filter → VideoSink 链，Builder 代码无需修改
