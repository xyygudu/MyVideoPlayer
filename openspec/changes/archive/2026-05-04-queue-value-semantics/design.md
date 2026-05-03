## Context

当前 PacketQueue/FrameQueue 的 Push/Pop 使用 C 风格裸指针接口，内部通过 `av_packet_move_ref`/`av_frame_ref` 做数据搬运。上一轮重构已引入 `AVPacketPtr`/`AVFramePtr` RAII 封装用于队列内部存储（`SerialPacket`/`SerialFrame`），但公共接口仍暴露裸指针。

## Goals / Non-Goals

**Goals:**
- 将 Push/Pop 接口改为 C++ 值语义，消除手动 `av_packet_move_ref`/`av_frame_move_ref`
- `SerialPacket`/`SerialFrame` 提升为 public，作为队列的传输单元
- Pop 返回 `std::optional` 替代 bool + out 参数

**Non-Goals:**
- 不改变队列的线程安全机制（mutex + condition_variable）
- 不改变 serial/flush/abort 语义
- 不修改 `AVFramePtr`/`AVPacketPtr` 本身的实现

## Decisions

### 1. Push 接收 `SerialPacket` 值而非 `AVPacketPtr&&` + int serial

**选择**：`void Push(SerialPacket sp)`

**备选方案**：`void Push(AVPacketPtr&& pkt, int serial)`

**理由**：两者对调用者的信息要求相同。直接传 `SerialPacket` 使得 Push/Pop 的类型对称（同一类型进出），代码一致性更好。未来增加字段时通过聚合初始化默认值即可，不破坏现有调用方。

### 2. Pop 返回 `std::optional<SerialPacket>` 而非 bool + out 参数

**选择**：`std::optional<SerialPacket> Pop()`

**备选方案**：保持 `bool Pop(SerialPacket* out)`

**理由**：optional 语义明确（nullopt = aborted），支持结构化绑定，消除调用者忘记检查返回值的风险。

### 3. FrameQueue 的 EOF 处理

**选择**：保留 `SerialFrame::eof` 字段，`PushEof` 推入 `eof=true` 的 SerialFrame

**理由**：显式 bool 比检查 `frame.get() == nullptr` 更清晰。Pop 返回 `optional<SerialFrame>` 后，调用者通过 `sf->eof` 判断 EOF，nullopt 表示 abort——三种状态（正常帧/EOF/abort）各有独立表达。

### 4. Demuxer 端构造方式

Demuxer 循环中每帧构造一个新的 `AVPacketPtr`：

```cpp
while (running) {
    AVPacketPtr pkt;
    av_read_frame(fmt_ctx, pkt.get());
    queue->Push(SerialPacket{std::move(pkt), serial});
}
```

每帧一次 `av_packet_alloc`（~80B 小对象），现代分配器下无可测量性能差异。

## Risks / Trade-offs

- **[ABI 破坏]** → 纯内部接口，无外部消费者，影响范围可控
- **[每帧堆分配]** → 实测无性能影响；必要时可引入 pool（非本次范围）
- **[optional 头文件开销]** → C++17 标准库已包含，无额外依赖
