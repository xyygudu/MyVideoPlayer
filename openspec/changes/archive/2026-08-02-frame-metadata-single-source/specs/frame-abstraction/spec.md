## MODIFIED Requirements

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

## REMOVED Requirements

### Requirement: MediaFrame 提供 CreateSameFormat 工厂方法
**Reason**: 零调用点。2026-07-24 的 effect-alloc-simd-opt 变更已用 `MediaFramePool::Acquire` 取代它（避免每帧 `av_frame_get_buffer` 的系统级分配），但函数与本需求都被遗留下来。其签名含 pts 参数，正处于本次元数据收敛的影响范围内。
**Migration**: 使用 `MediaFramePool::Acquire(width, height, format)`。
