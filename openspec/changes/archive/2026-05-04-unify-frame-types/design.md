## Context

当前帧管线：Decoder 输出 `AVFramePtr` → `FrameQueue` 存 `SerialFrame { AVFramePtr, serial, eof }` → VideoRenderLoop 从队列 pop 后手动计算 PTS（依赖 `AVStream*`）→ 最后调 `FrameConverter::ToVideoFrame` 创建公共类型。

问题：显示层直接操作 FFmpeg 类型，`FrameConverter` 作为中间层不增加价值，AVStream 的生命周期被外泄到运行时路径。

## Goals / Non-Goals

**Goals:**
- 解码后立即产出 `VideoFrame`/`AudioFrame`，管线下游不再接触 FFmpeg 类型
- FrameQueue 模板化，存储公共帧类型 + 传输元数据（serial/eof）
- Decoder 通过值类型参数（`DecoderParams`）获取 time_base，不持有 `AVStream*`
- `VideoFrame::Impl` 使用 `AVFramePtr` 实现 RAII 一致性
- 消除 `FrameConverter` 类

**Non-Goals:**
- 不改变公共 API（`VideoFrame`/`AudioFrame` 的接口签名不变）
- 不引入 VideoFrame 的深拷贝支持（留待未来）
- 不拆分 Decoder 为 VideoDecoder/AudioDecoder（当前通用 Decoder 仍可胜任，差异通过输出构建方式参数化）
- 不改变 PacketQueue 的设计

## Decisions

### Decision 1: FrameQueue 模板化 + 显式实例化

**选择**：`FrameQueue<T>` 模板，在 .cc 中显式实例化 `VideoFrame` 和 `AudioFrame` 两个版本。

**备选**：
- A) 抽象基类 `class Frame` → 需要堆分配 + 虚函数开销 + 类型擦除
- B) Header-only 模板 → 编译时间增长，实现暴露

**理由**：显式实例化兼顾类型安全和编译隔离。只有 2 种实例，代码膨胀可忽略。

### Decision 2: QueueEntry<T> 作为传输包装

```cpp
template<typename T>
struct QueueEntry {
    T frame;        // VideoFrame 或 AudioFrame（move-only）
    int serial;
    bool eof{false};
};
```

serial 和 eof 是队列传输元数据，不属于帧本身——放在外层包装而非帧内部。

### Decision 3: DecoderParams 值类型参数

```cpp
struct DecoderParams {
    AVRational time_base;
    AVRational frame_rate;  // 用于 fallback delay 估算
};
```

Decoder 构造/OpenCodec 时传入，不持有任何外部指针。对标 MPV `mp_codec_params` 的做法。

### Decision 4: Decoder 输出帧构建

Decoder 解码循环内：
1. `avcodec_receive_frame` → 临时 `AVFramePtr`
2. 计算 `pts = frame->pts * av_q2d(params_.time_base)`
3. 构建 `VideoFrame`（或 `AudioFrame`）：内部 `av_frame_ref` + 设置 pts
4. Push `QueueEntry<T>{ std::move(vf), current_serial, false }`
5. 临时 AVFramePtr unref（作用域结束自动释放）

PTS 换算集中在 Decoder 内，下游只用 `frame.pts()`。

### Decision 5: VideoFrame::Impl 使用 AVFramePtr

```cpp
struct VideoFrame::Impl {
    AVFramePtr frame;   // RAII，替代裸 AVFrame* + 手写析构
    PixelFormat format;
    double pts;
};
```

构建时 `AVFramePtr` 默认构造已 `av_frame_alloc()`，用 `av_frame_ref(impl->frame.get(), src)` 复制引用。

### Decision 6: 废弃 FrameConverter

`FrameConverter::ToVideoFrame` 和 `ToAudioFrame` 的逻辑（`av_frame_ref` + PTS 换算 + 格式映射）迁移到 Decoder 内部的私有方法。删除 `frame_converter.h/cc`。

## Risks / Trade-offs

- **[风险] 被丢弃帧的构建开销** → `av_frame_ref` 是 O(1) 引用计数操作 + 48 字节 Impl 分配。25fps 下每秒最多 25 次，可忽略。如果未来成为瓶颈可引入对象池。
- **[风险] AudioRenderer 改动面大** → SDL 回调中从 `AVFrame*` 直接访问改为 `AudioFrame` 访问器。功能等价但需逐行验证音频格式处理路径。
- **[风险] 模板化导致编译错误传播** → 使用显式实例化限制在 frame_queue.cc 中，编译错误定位清晰。
- **[取舍] Decoder 仍为通用类** → 不拆分 VideoDecoder/AudioDecoder，通过模板参数或回调区分输出类型。如果未来 video/audio 解码逻辑进一步分化再拆分。
