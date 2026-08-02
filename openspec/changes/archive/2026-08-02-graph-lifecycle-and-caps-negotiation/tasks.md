## 1. 生命周期骨架

- [x] 1.1 `NodeState` 新增 `kOpened`；`INode` 新增 `virtual bool Open()`（默认 true）与 `virtual void DeclareCaps()`（默认 no-op）
- [x] 1.2 `MediaGraph::Open()`：拓扑排序后按序调用节点 `Open()`，失败时 Stop 已 Open 的节点并返回 false
- [x] 1.3 `MediaGraph::AddNode()` 自动向节点注入 graph 引用（`INode::Attach`）
- [x] 1.4 两个 facade 改为显式 `Open() → Negotiate() → Prepare()`

## 2. 两趟协商与 caps

- [x] 2.1 `FormatCaps` 新增 `HeaderPlacement { kAny, kGlobal, kInBand }`，并新增 `Compatible()` 按"空=无约束"语义做无歧义校验
- [x] 2.2 `MediaGraph::Negotiate()` 改为三步：逆拓扑序 `DeclareCaps()` → 图级 caps 兼容性校验（冲突即失败）→ 拓扑序 `Negotiate()`
- [x] 2.3 移除 `OutputPort::Connect()` 中的 caps 检查

## 3. 节点迁移

- [x] 3.1 `DemuxNode`：`OpenFile()` 由 `Negotiate()` 移入 `Open()`，`Negotiate()` 仅读 codecpar 发布格式并做索引越界校验
- [x] 3.2 `MuxNode`：`ResolveOutputRequirements` 移入 `DeclareCaps()`，改为声明 `HeaderPlacement`
- [x] 3.3 `EncoderNode`：`Negotiate()` 读下游 caps 得出 header placement 结论，`Prepare()` 据此设置 `AV_CODEC_FLAG_GLOBAL_HEADER`
- [x] 3.4 `VideoSinkNode`：`Negotiate()` 校验输入格式并读取 frame_rate；删除 `SetVideoFps`
- [x] 3.5 `AudioSinkNode`：`ReadAudioParams()` 由 `Prepare()` 移入 `Negotiate()`

## 4. 清理

- [x] 4.1 删除 `InputPort::SetNeedsGlobalHeader/NeedsGlobalHeader` 及其成员
- [x] 4.2 删除死代码 `graph::StreamInfo`
- [x] 4.3 删除 4 处 `SetGraph` 调用与节点上的对应 setter

## 5. 验证

- [x] 5.1 `cmake --build build` 通过，`get_errors` 无告警
- [x] 5.2 `mvp_transcode_cli` 回归：mkv（HeaderPlacement=kGlobal，extradata 46B）与 mpegts（kInBand）均完整转码成功
- [x] 5.3 启动 `mvp_app` 无报错（播放画面/音视频同步由人工验收）
