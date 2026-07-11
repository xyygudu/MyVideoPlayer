## 1. 新增 SourceInfo 结构体

- [x] 1.1 创建 `src/core/include/mvp/source_info.h`，定义 `VideoStream`、`AudioStream`、`SourceInfo` 结构体
- [x] 1.2 `SourceInfo` 中 `video_streams` / `audio_streams` 使用 `std::vector`，支持多流场景
- [x] 1.3 `codec_name` 使用 `std::string` 而非 `AVCodecID`，解耦 FFmpeg 枚举

## 2. 新增 SourceProbe 探帧工具

- [x] 2.1 创建 `src/core/include/mvp/source_probe.h`，声明 `SourceProbe` 类及 `static SourceInfo Probe(const std::string& filepath)` 方法
- [x] 2.2 创建 `src/core/src/source_probe.cc`，实现 Probe：`avformat_open_input` → `avformat_find_stream_info` → 遍历流填充 SourceInfo → `avformat_close_input`
- [x] 2.3 探测失败时返回空 SourceInfo（video_streams / audio_streams 均为空）
- [x] 2.4 CMakeLists.txt 使用 GLOB_RECURSE 自动发现 source_probe.cc，无需手动添加

## 3. 修改 DemuxNode（不再构造时打开文件）

- [x] 3.1 `DemuxNode` 构造函数移除 `InitStreamInfo()` 调用，改为接收附加的流索引参数（视频流 index、音频流 index）
- [x] 3.2 `DemuxNode::Prepare()` 中的 `OpenFile()` 成为唯一打开路径（保留幂等守卫）
- [x] 3.3 移除 `StreamInfoMap()` 方法和 `stream_info_map_` 成员（唯一调用方是 BuildGraph）
- [x] 3.4 `Negotiate()` 中增加 `OpenFile()` 调用（格式协商需要 format_ctx_），`MakeStreamFormat` 保留（Negotiate 仍使用）

## 4. 重构 BuildGraph

- [x] 4.1 在 `BuildGraph` 开头调用 `SourceProbe::Probe(filepath)` 替代 `demux->StreamInfoMap()`
- [x] 4.2 从 `SourceInfo` 提取 `video_fps_`、`duration_`、`has_audio_`、`sink_count_`
- [x] 4.3 节点创建（DemuxNode / DecoderNode / SinkNode）内聚在一个代码块中，配置紧接着构造完成
- [x] 4.4 连线保持 inline `graph_->Connect(...)`，不封装为独立函数
- [x] 4.5 移除旧的裸指针变量森林和 `streams_` 成员

## 5. 验证与清理

- [x] 5.1 编译通过（`cmake --build build`）
- [x] 5.2 功能回归：播放含视频+音频的文件、仅视频文件、仅音频文件
- [x] 5.3 功能回归：暂停/恢复、Seek、播放到 EOF 后重播
- [x] 5.4 确认 `DemuxNode::StreamInfoMap()` 已移除（无残留调用）
