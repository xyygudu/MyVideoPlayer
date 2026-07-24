# 管线接口设计待改进点

> 记录时间：2026-05-17
> 关联变更：openspec/changes/unify-stream-pipeline

---

## 1. FrameQueue 去模板化

### 问题

`FrameQueue<T>` 仍为模板类，但 unify-stream-pipeline 重构后内部实际只使用 `FrameQueue<MediaFrame>` 一种实例化。`FrameQueue<VideoFrame>` 和 `FrameQueue<AudioFrame>` 的显式实例化已成死代码。

### 影响场景

- **编译开销**：frame_queue.cc 中仍保留 3 份显式实例化，增加无意义的编译产物
- **头文件复杂度**：使用方需要 `template<typename T> class FrameQueue` 前向声明，增加认知负担
- **新增 frame 类型时**：若未来加字幕帧，不需要再添加显式实例化

### 改进建议

将 `FrameQueue<T>` 改为非模板 `class FrameQueue`，内部固定持有 `std::queue<QueueEntry>`，其中 `QueueEntry` 也去模板化：

```cpp
struct QueueEntry {
    MediaFrame frame;
    int serial;
    bool eof{false};
};

class FrameQueue { ... };
```

删除 frame_queue.cc 中的显式实例化，简化头文件前向声明。

---

## 2. EofOutputCallback 的 serial 参数职责边界

### 问题

`EofOutputCallback = std::function<void(int serial)>` 将 decoder 内部的 serial 概念暴露给外部回调消费者。当前调用链：

```
AVFrameDecoder::DecodeLoop() → on_eof_(last_serial_)
  → StreamContext lambda → frame_queue_.PushEof(serial)
    → Player VideoRenderLoop: if (entry->eof) { break; }  // 未检查 serial!
```

Player 作为最终消费者并未校验 EOF 的 serial，存在潜在 bug：Seek 后旧 epoch 的 EOF 可能被误判为当前流结束。

### 影响场景

- **快速连续 Seek**：decoder 旧 epoch drain 完毕触发 EOF，Player 无条件标记 `video_eof_ = true`，导致播放提前终止
- **接口语义模糊**：外部使用 EofOutputCallback 时不理解 serial 含义，违反最小知识原则

### 改进建议

**方案 A（推荐）**：保留 serial 参数，但在 Player 消费 EOF 时增加 serial 校验：

```cpp
if (entry->eof) {
    if (entry->serial != video_ctx_->GetPacketQueue()->serial()) {
        continue;  // 过时的 EOF，忽略
    }
    video_eof_.store(true, ...);
    break;
}
```

**方案 B**：移除 EofOutputCallback 的 serial 参数，改为在 StreamContext 内部过滤（对比 PushEof 时的 serial 与 PacketQueue 当前 serial），只有匹配时才入队 EOF marker。外部回调简化为 `std::function<void()>`。

---

## 3. IDecoder::Start() 回调传递方式的可扩展性

### 问题

当前 `IDecoder::Start(PacketQueue*, MediaFrameCallback, EofOutputCallback)` 将所有回调作为 Start() 参数传入。当前只有 2 个回调，但如果未来需要增加 `OnError`、`OnFlushComplete`、`OnMetadata` 等回调，参数列表会膨胀。

### 影响场景

- **新增回调**：每增加一个回调需修改接口签名，所有实现类和调用点均需改动（违反开闭原则）
- **测试困难**：mock 时需要构造所有回调，即使测试只关心其中一个

### 改进建议

**当前结论**：2 个回调无需过度设计，保持现状。

**未来触发条件**：当回调数量达到 3 个时，改为 struct 聚合：

```cpp
struct DecoderCallbacks {
    MediaFrameCallback on_frame;   // required
    EofOutputCallback on_eof;      // required
    // 未来可选扩展：
    // ErrorCallback on_error;
    // FlushCallback on_flush_complete;
};

void Start(PacketQueue* queue, DecoderCallbacks callbacks);
```

或改为 Set 方式（但需确保 Start 前所有 required 回调已设置，增加运行时检查成本）。

---

## 4. MediaBuffer 构造函数成员初始化顺序读到 move 后的值

> 记录时间：2026-07-23
> 关联变更：openspec/changes/effect-alloc-simd-opt（排查 CPU 效果节点性能问题时顺带发现，不在该变更范围内修复）

### 问题

`MediaBuffer::MediaBuffer(MediaFrame frame, Timestamp ts, BufferFlags flags)` 的成员初始化列表写作：

```cpp
MediaBuffer::MediaBuffer(MediaFrame frame, Timestamp ts, BufferFlags flags)
    : payload_(std::move(frame)),
      media_type_(frame.type()),  // 在 payload_ 之后按声明顺序求值
      ...
```

C++ 成员初始化顺序按**声明顺序**（而非初始化列表书写顺序）执行。`media_buffer.h` 中 `payload_` 声明在 `media_type_` 之前，因此 `payload_(std::move(frame))` 先执行——这一步通过 `std::variant` 转换构造函数移动构造出 `MediaFrame`，触发 `MediaFrame` 的移动构造函数，其中会把源对象的 `type_` 重置为 `MediaType::kUnknown`。等到 `media_type_(frame.type())` 求值时，`frame` 已经是移动后的状态，读到的 `type()` 恒为 `kUnknown`，而不是调用方传入帧的真实类型。

### 影响场景

- **所有携带 `MediaFrame` payload 的 `MediaBuffer` 构造**：`media_type_` 字段实际上从未被正确设置为 `kVideo`/`kAudio`，无论传入什么类型的帧
- **依赖 `MediaBuffer::media_type()` 做路由/校验的逻辑**：如果未来新增基于 `media_type()` 的分支判断（当前代码路径主要靠 graph 连接拓扑区分音视频，可能尚未被这个 bug 影响到实际行为，但这是运气而非设计保证）

### 改进建议（参考 GStreamer GstBuffer 的字段初始化顺序）

在成员初始化列表里避免"先移动、后读取同一个源对象"的顺序依赖。最直接的修复是提前缓存要读取的值：

```cpp
MediaBuffer::MediaBuffer(MediaFrame frame, Timestamp ts, BufferFlags flags)
    : payload_(std::move(frame)),
      media_type_(std::get<MediaFrame>(payload_).type()),  // 从移动后的目标读取，而非移动前的源
      ...
```

或者更根本地，在初始化列表之外、构造函数体之前用一个局部变量固定住 `type()`：

```cpp
MediaBuffer::MediaBuffer(MediaFrame frame, Timestamp ts, BufferFlags flags)
    : media_type_(frame.type()),   // 依赖声明顺序调整：media_type_ 需要声明在 payload_ 之前
      payload_(std::move(frame)),
      ...
```

（后者要求同步调整 `media_buffer.h` 里 `payload_`/`media_type_` 的声明顺序，属于跨越声明处的改动，需要一并修改头文件。）

