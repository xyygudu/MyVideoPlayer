# Sink 线程模型与渲染待改进点

> 记录时间：2026-08-02
> 对标参考：FFplay (FFmpeg)
> 关联讨论：change seek-epoch-and-pause-step 评审时确认的范围外问题

---

## 1. 两个 Sink 的暂停等待都是轮询，且唤醒原因不显式

### 问题

`VideoSinkNode::RenderLoop` 与 `AudioSinkNode::AudioLoop` 在暂停时都用固定睡眠轮询：

```cpp
// VideoSinkNode
if (paused && !step) { std::this_thread::sleep_for(10ms); continue; }
// AudioSinkNode
if (ShouldThrottle()) { std::this_thread::sleep_for(5ms); continue; }
```

暂停期间线程每秒空转 100 次 / 200 次。更重要的是**唤醒原因不显式**：想让暂停态响应新事件（seek 前进一帧、窗口重绘、将来的逐帧播放）只能不断往循环里加 atomic 标志，每加一个都要在 `if` 条件里再串一项。

### 影响场景

- **响应延迟**：暂停态下任何交互（seek 预览、resize 重绘）最坏要等一个轮询周期
- **可扩展性**：逐帧前进/后退、暂停态截图、暂停态滤镜预览等功能每加一个就多一个标志位与一个 `if` 分支，正是「反补丁编码」要避免的分支堆叠
- **功耗**：暂停时仍持续唤醒线程

### 改进建议

改为条件变量，把所有唤醒原因收敛成一个显式谓词：

```cpp
std::unique_lock lk(pause_mutex_);
pause_cv_.wait(lk, [this] {
    return !paused_ || step_ || redraw_ || !running_;
});
```

**注意必须两个 sink 一起改**：AudioSinkNode 的等待条件除暂停外还包含 SDL 缓冲水位（`SDL_GetAudioStreamQueued` 超过 100ms 即回压），这是一个**没有事件通知的外部条件**，无法直接用条件变量表达 —— 需要先设计一个统一的等待抽象（例如带超时的 `WaitUntil(predicate, timeout)`），否则只改视频侧会让两个 sink 的暂停实现不对称。

因涉及等待抽象设计，本项未纳入 change seek-epoch-and-pause-step，留待两个 sink 一并处理。

---

## 2. VideoRenderer 的窗口尺寸跨线程非原子读写

> **已解决（2026-08-02，change sink-device-boundaries）**：尺寸变更改走 `kResize` 命令，由 VideoSinkNode 在渲染线程上应用。`window_width_/height_` 从此只有渲染线程一个写者，竞态从根消失，**且未引入任何原子成员** —— 竞态在正确的接缝处消失，而非靠加原子掩盖。下文保留作为问题记录。

### 问题

`VideoRenderer::Resize()` 由 Qt UI 线程调用，只记录尺寸：

```cpp
void VideoRenderer::Resize(int width, int height) {
    window_width_ = width;
    window_height_ = height;
}
```

而 `window_width_` / `window_height_` 由**渲染线程**在 `ComputeDestRect()` 中读取。两个普通 `int` 被两个线程无同步地读写 —— 这是标准的数据竞争（C++ 标准下属未定义行为），不是"读到旧值"这么温和。

### 影响场景

- **拖拽窗口边缘**：resize 事件高频触发，渲染线程可能读到撕裂或过期的尺寸组合（例如新宽度配旧高度），画面短暂拉伸变形
- **UB 性质**：编译器可以合法地把循环内的读提到循环外并缓存，导致 resize 长期不生效，且在 Debug 构建下不复现
- **风险随暂停重绘上升**：change seek-epoch-and-pause-step 引入 `kRedraw` 后，UI 线程写尺寸与渲染线程读尺寸的时间距离更近

### 改进建议

最小修复是把两个字段改为 `std::atomic<int>`（宽高各自原子仍可能读到不匹配的组合，但至少消除 UB）：

```cpp
std::atomic<int> window_width_{0};
std::atomic<int> window_height_{0};
```

若要连"宽高不匹配"一并解决，可把二者打包进一个 `std::atomic<uint64_t>`（高 32 位宽、低 32 位高）一次性读写，或复用项目已有的 SeqLock 模式。

未纳入 change seek-epoch-and-pause-step，因该竞态先于本次改动存在，且位于 VideoRenderer 而非 graph 层。

---

## 3. link-capacity spec 与实现不符

> **已解决（2026-08-02，change link-capacity-must-be-explicit）**：spec 与实现同时修正，容量必须显式声明且不可为无界。

### 问题

`openspec/specs/link-capacity/spec.md` 中有一条场景：

> **Scenario: Demux→Decoder 不因视频包阻塞音频路径**
> - **WHEN** Demux 读取交织的 A/V 包
> - **THEN** Packet 链路有足够容量（15MB / 256 包），Demux 不会被视频包反压阻塞

实现上并非如此：`Link::Push` 在队列满时**确实会阻塞 demux 线程**，此时另一条支路即使空着也拿不到数据。"容量足够"只是降低了发生概率，不是机制保证。

### 影响场景

- spec 描述了一个实现并不提供的保证，后续改动可能基于错误前提
- 与 `docs/improvements/link-and-buffer-design.md` 第 3 条（生产者自我节流）是同一问题的两面

### 改进建议

两条路二选一：
- 若采纳 link-and-buffer-design 第 3 条的「生产者自我节流」改造，该场景成为真实保证，spec 可保留
- 若不改造，spec 应改为如实描述：容量参数只是降低支路互相饿死的概率

---

## 4. 设备锁包住整个 avcodec 调用，粒度过粗

> **已解决（2026-10-01，change gpu-stage-b-gpu-effects）**：递归锁在 `av_hwdevice_ctx_init` 前交给 FFmpeg（`AVD3D11VADeviceContext.lock/unlock`），锁随 FFmpeg 设备上下文释放；解码器不再加锁，GPU pass、呈现拷贝、SDL 绘制共用这一把锁。下文保留作为问题记录。

> 记录时间：2026-09-30，关联 change gpu-stage-a-hw-decode-render（seek 冻结排查）

### 问题

`GpuDevice::DeviceContextMutex()` 由应用层在 DecoderNode 中包住**整个** `avcodec_send_packet` / `avcodec_receive_frame` / `avcodec_flush_buffers`，渲染线程的 `VideoRenderer::Render` 也拿同一把锁。FFmpeg 内部另有一把自己的 D3D11 设备锁（`AVD3D11VADeviceContext.lock`，默认是内部 Windows mutex），两把锁互不知情：

- 解码调用中的纯 CPU 部分（码流解析、slice 头处理、PTS 计算）也把渲染线程挡在门外
- 解码一旦卡在驱动里，渲染线程一定跟着卡死（seek 冻结时正是这样表现为「画面不刷新」）
- 两把锁并存，将来若有代码在持有其一时去拿另一把，就有锁序反转风险

### 影响场景

- **4K 高码率播放**：关键帧解码持锁 10ms+，渲染线程 Present 被推迟，出现偶发掉帧
- **故障放大**：解码侧任何阻塞都直接冻结画面，而不是只影响解码

### 改进建议（参考 VLC d3d11va `context_mutex` / FFmpeg `AVD3D11VADeviceContext.lock` 文档）

FFmpeg 文档说明 `lock/unlock` 回调「保护对 device_context 与 video_context 的访问」，且**必须是递归锁**。在 `D3D11GpuDevice::Wrap` 中于 `av_hwdevice_ctx_init` 之前把设备锁交给 FFmpeg，让 FFmpeg 只在真正提交 D3D11 命令时加锁；应用层只保留 blit 与渲染两处：

```cpp
// D3D11GpuDevice::Wrap
hwctx->lock_ctx = this;
hwctx->lock   = [](void* c) { static_cast<D3D11GpuDevice*>(c)->context_mutex_.lock(); };
hwctx->unlock = [](void* c) { static_cast<D3D11GpuDevice*>(c)->context_mutex_.unlock(); };
// context_mutex_ 改为 std::recursive_mutex

// DecoderNode：删除 send/receive/flush 外层的 DeviceLock，仅 CopyForPresentation 保留
```

前提：共享设备已开启多线程保护（已完成），FFmpeg 的 `d3d11va_transfer_data` 等路径同样走该锁，特效节点的下载也因此被正确串行化。

---

## 5. 渲染器与 GPU 设备每个文件重建

> 记录时间：2026-10-01，关联 change gpu-stage-b-gpu-effects（design Open Questions）

### 问题

`MediaPlayer::Open` 每次先 `Close()`，`VideoRenderer::Close/Open` 随之销毁并重建 SDL 窗口、SDL 渲染器、D3D11 设备、`GpuDevice` 包装与呈现器。设备是会话级资源，却跟着文件生灭。

### 影响场景

- **打开文件延迟**：每次打开都要重建设备与交换链，GPU pass 的着色器、常量缓冲也随之重建
- **切换文件闪烁**：窗口与交换链重建期间画面空白

### 改进建议（参考 MPV VO 生命周期 / VLC vout 复用）

mpv 的 VO 与 hwdec 设备跨播放列表条目存活，只有输出参数不兼容时才重配；VLC 在格式兼容时复用 vout 与 decoder device。让 `VideoRenderer` 在窗口句柄不变时跨文件保持打开，`MediaPlayer::Close` 只销毁 graph；GPU pass 若也要跨文件复用，可由设备提供着色器/管线缓存（GStreamer `GstD3D11Device` 缓存 ps/vs/sampler）。

---

## 6. 硬件帧每次呈现都新建 SDL 包装纹理

### 问题

`VideoRenderer::RenderBoundTexture` 每帧 `SDL_CreateTextureWithProperties` 包装外部纹理（SDL 内部为其创建两个 SRV），画完即销毁。

### 影响场景

- **4K60 播放**：每秒 60 次纹理包装对象与 SRV 的创建/释放，驱动调用微秒级，目前可接受
- **高帧率/多路视频**：开销线性增长

### 改进建议（参考 GStreamer `GstD3D11Memory` 视图缓存 / mpv `ra_hwdec_mapper`）

呈现器自有的拷贝纹理指针稳定，可长期缓存其 SDL 包装；GPU pass 输出纹理来自 FFmpeg 帧池、会被复用，可按纹理指针做小容量缓存（包装对象持有纹理引用，指针不会被复用到别的资源上）。

