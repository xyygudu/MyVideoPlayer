## MODIFIED Requirements

### Requirement: MediaFormat 用 variant 拆分音视频
MediaFormat SHALL 用 `std::variant<std::monostate, EncodedFormat, VideoFormat, AudioFormat>` 承载类型专属字段，消除扁平胖结构体。

```cpp
struct EncodedFormat { int codec_id; std::shared_ptr<AVCodecParameters> codec_params; };
struct VideoFormat   { int width, height; PixelFormat pixel_format; Rational frame_rate; };
struct AudioFormat   { int sample_rate, channels; SampleFormat sample_format; };
```

公共字段 `media_type` 和 `time_base` SHALL 留在 MediaFormat 外层（路由和时间换算需要）。`codec_params` SHALL 只在 EncodedFormat 分支，裸帧格式（Video/Audio）不携带。

访问 SHALL 通过类型安全方法：`IsVideo()/IsAudio()/IsEncoded()` 判定，`AsVideo()/AsAudio()/AsEncoded()` 取对应分支。SHALL NOT 保留 `fmt.width()` 等扁平转发方法——强制调用方先确立类型。

#### Scenario: 编码格式只带 codec_params
- **WHEN** DemuxNode 输出端口设置格式
- **THEN** 格式为 EncodedFormat，携带 codec_params；AsEncoded().codec_params 有效

#### Scenario: 裸视频格式不带 codec_params
- **WHEN** DecoderNode 输出端口设置格式
- **THEN** 格式为 VideoFormat，含 width/height/pixel_format，无 codec_params 槽

#### Scenario: 类型安全访问
- **WHEN** 对一个 AudioFormat 调用 AsVideo()
- **THEN** std::get 抛出（错误分支），暴露类型误用

#### Scenario: 调用点强制确立类型
- **WHEN** 读取视频宽度
- **THEN** 调用方写 `fmt.AsVideo().width`，不能写 `fmt.width()`

### Requirement: FormatCaps Intersect 泛型化
FormatCaps::Intersect SHALL 用泛型辅助 `IntersectVectors<T>(a, b)` 消除 6 个集合的重复 find-push 逻辑，函数体收缩到 50 行以内。

#### Scenario: 泛型交集复用
- **WHEN** 计算 pixel_formats/sample_formats/codec_ids 等集合交集
- **THEN** 统一调用 IntersectVectors<T>，无重复样板代码

### Requirement: INode 接口控制方法
INode SHALL 新增 `virtual void OnCommand(const Command& cmd) {}` 默认空实现。MediaGraph SHALL 新增 `Seek(double)`、`SetPaused(bool)`、`SendCommand(const Command&)` 高层操作。

#### Scenario: 节点默认忽略无关命令
- **WHEN** 节点未覆写 OnCommand 或不响应某命令类型
- **THEN** 默认空实现安全忽略

### Requirement: MediaGraph 缓存源时长元数据
MediaGraph 或 MediaPlayer SHALL 缓存源 duration（来自 Probe），供 Duration 查询，不再运行时访问 DemuxNode。

#### Scenario: 时长查询不访问节点
- **WHEN** 查询时长
- **THEN** 读取缓存值，不调用 demux_node_->Duration()
