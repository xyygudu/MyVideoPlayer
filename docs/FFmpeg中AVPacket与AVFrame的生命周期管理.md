# FFmpeg 中 AVPacket 与 AVFrame 的生命周期管理

> 基于 FFmpeg 7.1 的 send/receive API，结合本项目（MyVideoPlayer）多线程 demux → decode → render 管线的实际代码进行分析。

---

## 1. 核心概念：壳与数据的分离

FFmpeg 的 AVPacket 和 AVFrame 都采用**两层结构**设计：

- **壳（结构体）**：存放元数据（pts、size、stream_index 等）和一个指向数据缓冲区的指针
- **数据缓冲区（AVBufferRef）**：存放实际的压缩/解压数据，通过**引用计数**管理生命周期

> 📊 可视化版本见 [avpacket-avframe-lifecycle.drawio](avpacket-avframe-lifecycle.drawio) → **Page 1: 壳与数据的两层结构**

```
┌──────────────────────────┐
│  AVPacket / AVFrame 壳    │  ← alloc 分配的是这个
│  ┌──────────────┐        │
│  │ *buf ─────────┼────────────► AVBufferRef (refcount=N)
│  │ *data         │        │       [实际数据: H.264 NAL / PCM / RGB ...]
│  │ size, pts ... │        │
│  └──────────────┘        │
└──────────────────────────┘
```

### 关键 API 对比

| 操作 | AVPacket | AVFrame | 语义 |
|------|----------|---------|------|
| 分配空壳 | `av_packet_alloc()` | `av_frame_alloc()` | 只分配结构体，不分配数据缓冲区 |
| 浅拷贝 + 引用计数 +1 | `av_packet_ref(dst, src)` | `av_frame_ref(dst, src)` | dst 和 src 共享同一份数据 |
| 移动所有权 | `av_packet_move_ref(dst, src)` | `av_frame_move_ref(dst, src)` | 数据转移到 dst，src 变空壳（零拷贝） |
| 释放引用 | `av_packet_unref(pkt)` | `av_frame_unref(frame)` | refcount-1，清空壳。refcount 到 0 时释放数据 |
| 释放壳 + 数据 | `av_packet_free(&pkt)` | `av_frame_free(&frame)` | unref + 释放壳本身，指针置 NULL |

### `av_packet_alloc` 的常见误解

`av_packet_alloc` **只分配结构体**（约 80 字节），不分配数据缓冲区：

```c
AVPacket *pkt = av_packet_alloc();
// pkt->buf  = NULL   ← 没有数据
// pkt->data = NULL
// pkt->size = 0
```

数据缓冲区由 `av_read_frame` 在内部分配，填充到 pkt 的 `buf` 字段中。`av_frame_alloc` 同理。

---

## 2. `move_ref` 的本质：零拷贝转移

`av_packet_move_ref` 和 `av_frame_move_ref` 的内部实现等价于：

```c
void av_packet_move_ref(AVPacket *dst, AVPacket *src) {
    *dst = *src;           // 浅拷贝整个结构体（包括 buf 指针）
    av_init_packet(src);   // 把 src 重置为空壳
}
```

**数据缓冲区在内存中的位置没有变**，只是"属于谁"发生了转移。类似 C++ 的 `std::move`，引用计数不变。

---

## 3. AVPacket 在本项目中的完整流转

### 3.1 数据流全景

> 📊 可视化版本见 [avpacket-avframe-lifecycle.drawio](avpacket-avframe-lifecycle.drawio) → **Page 2: AVPacket 数据流全景**

```
DemuxLoop (demuxer.cc)          PacketQueue           DecodeLoop (decoder.cc)
─────────────────────          ────────────           ──────────────────────
pkt = av_packet_alloc()         queue_<AVPacket*>      pkt = av_packet_alloc()
        │                                                     │
        ▼                                                     │
av_read_frame(pkt)                                            │
  pkt.buf → [数据A] ref=1                                     │
        │                                                     │
        ▼                                                     │
queue.Push(pkt)                                               │
  copy = alloc()                                              │
  move_ref(copy, pkt)                                         │
  → copy.buf → [数据A]                                        │
  → pkt 变空壳                                                 │
  → copy 入队                                                  │
        │                                                     │
av_packet_unref(pkt)                                          │
  → pkt 已是空壳, no-op                                        │
  → 下次循环复用 pkt                                            │
                                                              │
                               queue.Pop(pkt) ◄───────────────┘
                                 move_ref(pkt, front)
                                 → pkt.buf → [数据A]
                                 free(&front)  // 释放空壳
                                                              │
                                                              ▼
                                               avcodec_send_packet(ctx, pkt)
                                                 → 内部 addref, ref=2
                                                              │
                                                              ▼
                                               av_packet_unref(pkt)
                                                 → ref=1, pkt 变空壳, 复用
                                                 → [数据A]仍存活(编解码器持有)
```

### 3.2 Demuxer 端代码（`demuxer.cc`）

```cpp
void Demuxer::DemuxLoop() {
    AVPacket* pkt = av_packet_alloc();  // 循环外分配一次空壳，全程复用

    while (running_) {
        int ret = av_read_frame(format_ctx_, pkt);  // 填充壳：pkt->buf 指向新数据
        if (ret < 0) break;

        if (pkt->stream_index == audio_stream_index_ && audio_queue_) {
            audio_queue_->Push(pkt);   // 所有权转移到队列，pkt 变空壳
        } else if (pkt->stream_index == video_stream_index_ && video_queue_) {
            video_queue_->Push(pkt);
        }
        av_packet_unref(pkt);  // 安全：如果 Push 已 move 走，这里是 no-op
                               //       如果 pkt 未被任何队列消费，这里释放数据
    }
    av_packet_free(&pkt);      // 循环结束，释放壳本身
}
```

### 3.3 PacketQueue 的所有权管理（`packet_queue.cc`）

```cpp
void PacketQueue::Push(AVPacket* pkt) {
    AVPacket* copy = av_packet_alloc();    // 为队列内部分配一个新壳
    av_packet_move_ref(copy, pkt);         // 数据从调用者的壳 move 到队列的壳

    std::unique_lock<std::mutex> lock(mutex_);
    // ... 等待队列有空间 ...
    queue_.push(copy);                     // 队列拥有 copy 壳及其数据
}

bool PacketQueue::Pop(AVPacket* pkt) {
    std::unique_lock<std::mutex> lock(mutex_);
    // ... 等待队列非空 ...
    AVPacket* front = queue_.front();
    queue_.pop();
    av_packet_move_ref(pkt, front);        // 数据从队列壳 move 到调用者壳
    av_packet_free(&front);                // 释放队列的空壳
    return true;
}
```

### 3.4 Decoder 端代码（`decoder.cc`）

```cpp
void Decoder::DecodeLoop() {
    AVPacket* pkt = av_packet_alloc();   // 循环外分配一次空壳

    while (running_) {
        if (!packet_queue_->Pop(pkt))    // 数据 move 到 pkt
            break;

        avcodec_send_packet(codec_ctx_, pkt);  // 编解码器内部对 buf addref
        av_packet_unref(pkt);                  // 释放本侧引用，pkt 变空壳复用
        // 数据仍存活——编解码器还持有引用
        // ...
    }
    av_packet_free(&pkt);
}
```

---

## 4. AVFrame 在本项目中的完整流转

### 4.1 与 AVPacket 的对称设计

FrameQueue 的实现与 PacketQueue **完全对称**：

```cpp
// frame_queue.cc
void FrameQueue::Push(AVFrame* frame) {
    AVFrame* copy = av_frame_alloc();
    av_frame_move_ref(copy, frame);   // 零拷贝转移
    // ... 入队 ...
}

bool FrameQueue::Pop(AVFrame* frame) {
    // ... 出队 ...
    av_frame_move_ref(frame, front);  // 零拷贝转移
    av_frame_free(&front);
    return true;
}
```

### 4.2 视频帧的特殊处理：RGB 转换

```cpp
// decoder.cc — 视频解码分支
sws_scale(sws_ctx_, frame->data, ..., rgb_frame->data, ...);
// sws_scale 将数据写入 rgb_frame 的 buffer（预分配的 rgb_buffer）

AVFrame* out = av_frame_alloc();
av_frame_ref(out, rgb_frame);     // ref 而非 move_ref，因为 rgb_frame 要复用
out->pts = frame->pts;
frame_queue_->Push(out);          // 队列内部会 move_ref
av_frame_free(&out);              // 释放 Push 之后的空壳
av_frame_unref(frame);            // 释放原始解码帧
```

这里用 `av_frame_ref`（拷贝引用）而非 `move_ref`，因为 `rgb_frame` 的 buffer 在整个循环中被反复写入。

---

## 5. Packet 与 Frame 的数量关系

**不是简单的 1:1 对应**，取决于编码格式和解码器内部缓冲：

| 场景 | 关系 | 说明 |
|------|------|------|
| 大多数音频（AAC/MP3） | 1 packet → 1 frame | 一个压缩包解码出一个音频帧 |
| 简单视频（I/P 帧） | 1 packet → 1 frame | 一个压缩包解码出一帧画面 |
| 含 B 帧的视频 | 送 packet 时返回 EAGAIN，后续才输出 frame | 解码器需要缓冲多个 packet 才能确定输出顺序 |
| 某些特殊编码器 | 1 packet → 0 或 N frames | 需要攒够数据，或一个 packet 产出多帧 |

这就是 FFmpeg send/receive API 的设计动机——**解耦 packet 和 frame 的 N:M 关系**：

```cpp
avcodec_send_packet(ctx, pkt);          // 送入 1 个 packet
while (...) {
    ret = avcodec_receive_frame(ctx, frame);  // 尝试取 frame
    if (ret == AVERROR(EAGAIN)) break;        // 还没攒够，下次再来
    // 处理 frame ...
}
```

调用者不需要关心解码器内部缓冲了多少 packet，只管"送入"和"取出"。

---

## 6. 多线程安全分析

### 需要保护的 vs 不需要保护的

| 层级 | 线程安全性 | 保护方式 |
|------|-----------|---------|
| AVPacket/AVFrame 壳（结构体字段） | **不安全** | PacketQueue/FrameQueue 的 mutex |
| AVBufferRef 的 refcount | **天然安全** | FFmpeg 内部使用原子操作 |
| 数据缓冲区的内容 | **只读**（写入后不再修改） | 不需要保护 |

### 本项目的保护策略

- **PacketQueue / FrameQueue** 用 `mutex` + `condition_variable` 保护队列操作
- 每个壳**同一时刻只属于一个线程**——`move_ref` 确保了所有权的独占转移
- FFmpeg 的 `AVBufferRef` refcount 是原子的，所以当 decoder 线程 `unref` 和 demuxer 线程操作另一个壳时不会冲突

---

## 7. 常见陷阱与最佳实践

### 陷阱

1. **忘记 `unref`**：每次 `av_read_frame` 或 `av_packet_ref` 后都必须有对应的 `unref`，否则内存泄漏
2. **`move_ref` 后继续使用 src**：src 已是空壳，访问 `src->data` 是 UB
3. **混淆 `ref` 和 `move_ref`**：`ref` 增加引用计数（共享数据），`move_ref` 转移所有权（独占数据）
4. **对空壳调用 `unref`**：虽然是安全的 no-op，但如果逻辑上不该为空壳，说明有 bug

### 最佳实践

1. **循环外 alloc，循环内复用**：避免每次迭代都 alloc/free
2. **Push 时 move 到队列内部的壳**：调用者的壳不进队列，避免生命周期纠缠
3. **Pop 时 move 到调用者的壳**：队列内部壳立即释放，所有权清晰
4. **全程零拷贝**：从 demuxer 到 decoder，数据始终在同一块内存，只有指针在不同的壳之间"搬家"

---

## 8. 总结：所有权转移全景图

> 📊 可视化版本见 [avpacket-avframe-lifecycle.drawio](avpacket-avframe-lifecycle.drawio) → **Page 3: 所有权转移全景图**

```
av_read_frame          PacketQueue         avcodec_send_packet      FrameQueue          渲染/播放
     │                   │                       │                     │                   │
     │  move_ref         │     move_ref          │                     │    move_ref       │
     ├──────────► Push ──┤──── Pop ──► pkt ──────┤                     │                   │
     │                   │                       │                     │                   │
     │                   │              avcodec_receive_frame           │                   │
     │                   │                       │     move_ref        │    move_ref       │
     │                   │                       ├──────────► Push ────┤──── Pop ──────────┤
     │                   │                       │                     │                   │
```

**核心原则：数据不动，壳在动。** 引用计数保证最后一个持有者释放时才真正回收内存。
