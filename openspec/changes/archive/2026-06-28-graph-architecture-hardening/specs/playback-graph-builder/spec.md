## ADDED Requirements

### Requirement: PlaybackGraphBuilder 封装播放图构建
系统 SHALL 定义 `PlaybackGraphBuilder`，封装播放场景的图构建逻辑，将 MediaPlayer::BuildGraph 的过程式代码提炼为专职 Builder。

Builder SHALL 通过 `PlaybackContext` 结构体注入依赖（避免长参数列表和同类型指针传错位）：
```cpp
struct PlaybackContext {
    MediaGraph* graph;
    VideoRenderer* renderer;
    Clock* audio_clock;
    Clock* video_clock;
    HWAccelContext* hw_device;
    void* window_handle;
    VideoFrameCallback video_cb;
};
```

#### Scenario: Builder 用 Context 注入依赖
- **WHEN** 创建 PlaybackGraphBuilder
- **THEN** 通过单个 PlaybackContext 参数传入所有依赖，字段具名

#### Scenario: BuildGraph 大幅瘦身
- **WHEN** MediaPlayer::BuildGraph 重构后
- **THEN** 收缩到约 15 行：Probe → builder 逐流 AddPipeline → graph Negotiate/Prepare

### Requirement: 滤镜就绪的链式管线构建
PlaybackGraphBuilder SHALL 提供 `AddVideoPipeline(const StreamInfo&, const std::vector<FilterSpec>& filters = {})` 和 `AddAudioPipeline(...)`，将管线构建为线性链：`Decoder → [filters...] → Sink`。

filters 列表 SHALL 默认为空（当前播放无滤镜）。Builder SHALL 用内部 `ConnectChain` 辅助方法按顺序连接链中节点，使滤镜插入只需向列表添加 FilterSpec，无需改 Builder 代码。

```cpp
struct FilterSpec { std::string name; std::string args; };
```

#### Scenario: 无滤镜管线
- **WHEN** AddVideoPipeline(stream) 传空 filters
- **THEN** 构建 Decoder → VideoSink 链并连接

#### Scenario: 滤镜就绪（将来）
- **WHEN** 未来 AddVideoPipeline(stream, {{"scale","1280:720"}}) 传非空 filters
- **THEN** 构建 Decoder → ScaleFilter → VideoSink 链，Builder 代码无需修改

#### Scenario: ConnectChain 复用
- **WHEN** 构建任意长度的节点链
- **THEN** ConnectChain 按相邻节点顺序逐对 Connect，预留 TranscodeGraphBuilder 复用
