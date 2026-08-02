## Requirements

### Requirement: VideoFrame and AudioFrame remain public API types
VideoFrame 和 AudioFrame SHALL 保持其现有接口完全不变。内部构造路径 SHALL 保持 MakeVideoFrame/MakeAudioFrame 工厂函数，调用位置从 FrameQueue 消费端变为 SinkNode 中。

### Requirement: FrameQueue de-templated into Link
FrameQueue 模板类的功能 SHALL 被 Link 模板替代。FrameQueue 的 QueueEntry 概念提升到 MediaBuffer 层面（payload + serial + flags）。

### Requirement: Format mapping resides at render boundary
像素格式映射和采样格式映射 SHALL 在 MakeVideoFrame/MakeAudioFrame 工厂函数中执行，不在管线传输层中执行。

### Requirement: MediaFrame 提供平面数据访问方法
`MediaFrame` SHALL 新增 `width()`/`height()`/`format()`/`PlaneData(int)`/`PlaneLinesize(int)` 方法，封装 AVFrame 内部平面布局访问，使调用方无需通过 `RawFrame()` 获取裸指针即可读写像素数据。

#### Scenario: 通过 PlaneData 访问 Y 平面
- **WHEN** 调用 `frame.PlaneData(0)` 和 `frame.PlaneLinesize(0)`
- **THEN** 返回值与 `av_frame` 内部 `data[0]`/`linesize[0]` 一致

### Requirement: MediaFrame 提供 MakeWritable 可写性保障
`MediaFrame` SHALL 提供两个 ref-qualified 重载：
- `[[nodiscard]] MediaFrame MakeWritable() &&`：消费调用者对 `*this` 的所有权。若底层帧此时唯一引用（`av_frame_is_writable` 为真），SHALL 直接移交所有权（不产生额外的 `AVBufferRef` 引用计数递增，不拷贝像素数据）；若非唯一引用，SHALL 回退为深拷贝（分配新缓冲区并拷贝像素数据）。
- `[[nodiscard]] MediaFrame MakeWritable() const &`：保留调用者持有的原对象不变，SHALL 始终返回独立的深拷贝副本（不做唯一引用检测优化），语义与本能力修改前一致。

调用方在明确不再需要原 `MediaFrame`（即将丢弃或已 move 出）时 SHALL 优先使用 `&&` 重载以避免不必要的拷贝。

#### Scenario: 唯一引用的帧调用 `&&` 重载零拷贝
- **WHEN** 某 `MediaFrame` 的底层 `AVFrame` 引用计数为 1（无其他持有者），调用 `std::move(frame).MakeWritable()`
- **THEN** 返回的 `MediaFrame` 与原底层缓冲区是同一块内存（未发生 `av_frame_ref`/拷贝），原对象的 `frame_` 变为已移出状态

#### Scenario: 共享帧调用 `&&` 重载仍触发深拷贝
- **WHEN** 某 `MediaFrame` 被多处引用（底层 `AVFrame` 引用计数 > 1），调用 `std::move(frame).MakeWritable()`
- **THEN** 返回的帧拥有独立的内存副本，不影响其他持有者手中的数据

#### Scenario: `const &` 重载始终拷贝，不做唯一引用优化
- **WHEN** 对同一个 `MediaFrame` 左值多次调用 `MakeWritable()`（未 move）
- **THEN** 每次调用都返回一个新的独立内存副本，原对象不受影响、可继续被读取

### Requirement: MediaFrame 提供 MediaFramePool 复用分配
系统 SHALL 定义 `MediaFramePool` 类型（move-only），内部封装一个 `AVBufferPool`，用于为"每帧需要一块同尺寸/格式输出缓冲区"的调用方（如效果节点）提供可复用的帧分配，避免每帧触发系统级内存分配。

`MediaFramePool` SHALL 提供 `MediaFrame Acquire(int width, int height, int format)`：
- 若请求的 width/height/format 与内部已建池的规格一致，SHALL 从现有 `AVBufferPool` 取出缓冲区构造 `MediaFrame`，不重新分配底层内存（除非池中暂无可复用缓冲区，此时按 `AVBufferPool` 自身语义分配新的一块并纳入池管理）
- 若请求的规格与内部当前池不一致（首次调用或分辨率/格式变化），SHALL 释放旧池并按新规格重建

`Acquire` SHALL NOT 接收时间戳 —— 帧是纯数据，时间由所属 `MediaBuffer` 承载。

#### Scenario: 连续请求相同规格复用底层缓冲区
- **WHEN** 对同一个 `MediaFramePool` 连续两次调用 `Acquire(1920, 1080, fmt)`，且第一次返回的 `MediaFrame` 在第二次调用前已被析构（引用计数归零，缓冲区回归池）
- **THEN** 第二次调用不触发新的系统级内存分配，而是复用池中已存在的缓冲区

#### Scenario: 规格变化时重建池
- **WHEN** 先以 `Acquire(1920, 1080, fmt)` 请求，随后以 `Acquire(3840, 2160, fmt)` 请求
- **THEN** 内部旧池被释放，按新分辨率重建池并返回正确尺寸的 `MediaFrame`

#### Scenario: 取得的帧不含时间信息
- **WHEN** 调用 `Acquire` 取得一个输出帧
- **THEN** 该帧只是像素缓冲区，调用方在构造 `MediaBuffer` 时提供时间戳
