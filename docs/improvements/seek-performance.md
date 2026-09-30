# Seek 性能待改进点

> 记录时间：2026-05-05
> 对标参考：MPV (player/playloop.c, demux/demux.c, filters/f_decoder_wrapper.c)
> 背景：2K 60fps H.264 视频 seek 延迟约 0.3-0.5s，MPV 几乎瞬间完成

### Profiling 结论（VS 诊断工具 2026-05-05）

seek 期间 CPU 采样显示：**78.74% 时间在 `Decoder::DecodeLoop`**，其中几乎全部（78.16%）消耗在 `avcodec-61.dll` 内部（H.264 软解码）。队列 push/pop、条件变量、I/O 开销在 profile 中几乎不可见。

**真正瓶颈是 2K H.264 软解码 CPU 开销**：seek 到 GOP 中间时必须解码前面所有参考帧（帧间依赖），每帧 2K 分辨率软解约 5ms，一个 GOP 120-300 帧 → 600ms-1500ms。

---

## 1. 未启用硬件解码，纯 CPU 软解 2K H.264（最大瓶颈）

> **已解决（2026-09-30，change gpu-stage-a-hw-decode-render）**：D3D11VA 硬解 + 零拷贝呈现，4K H.264 硬解约 240 帧/秒。剩余延迟见第 9 项。

### 问题

当前 `Decoder::Open` 直接使用软件解码器（`avcodec_find_decoder`），2K H.264 每帧软解约 5ms。seek 时需要解码整个 GOP 前部的参考帧（60fps, GOP=2-5s, 120-300 帧），纯 CPU 计算耗时 600ms+。

### 影响场景

- **2K/4K 视频 seek**：profile 实测 78% CPU 时间在 avcodec 软解码
- **正常播放高码率视频**：CPU 占用高，笔记本风扇狂转
- **4K 60fps HEVC**：软解完全无法实时

### 改进建议（参考 MPV 默认启用 `--hwdec=auto`）

使用 D3D11VA (Windows) 硬件解码，GPU 解码 2K H.264 每帧 < 0.1ms，比软解快 50 倍：

```cpp
bool Decoder::Open(AVStream* stream) {
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    codec_ctx_ = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx_, stream->codecpar);

    // 尝试启用 D3D11VA 硬解
    AVBufferRef* hw_device_ctx = nullptr;
    if (av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_D3D11VA,
                               nullptr, nullptr, 0) == 0) {
        codec_ctx_->hw_device_ctx = av_buffer_ref(hw_device_ctx);
        av_buffer_unref(&hw_device_ctx);
    }

    avcodec_open2(codec_ctx_, codec, nullptr);
}
```

注意：硬解输出帧格式为 NV12/D3D11 texture，需要额外的 format transfer 或直接 GPU 渲染。实现复杂度较高但收益最大。

---

## 2. Seek 期间未使用 `skip_frame` 跳过非参考帧（低难度高收益）

> **部分解决**：软解已启用 `AVDISCARD_NONREF`；硬解未启用，见第 11 项。

### 问题

seek 期间 FFmpeg 解码器默认解码所有帧（包括 B 帧等非参考帧）。这些非参考帧对后续帧的解码无贡献，seek 时解码它们纯属浪费 CPU。

### 影响场景

- **典型 H.264 IBP 结构**：B 帧占比 50-70%，seek 期间全部解码是浪费
- **profile 实测**：78% 时间在解码，跳过 B 帧可减少约一半解码帧数

### 改进建议（参考 MPV `hrseek_framedrop` / FFplay `framedrop`）

seek 开始时设置 `skip_frame`，到达目标帧后恢复：

```cpp
// seek 发起时
codec_ctx_->skip_frame = AVDISCARD_NONREF;  // 只解码参考帧（I + P）

// DecodeLoop 中，当帧 pts >= seek_target 时
codec_ctx_->skip_frame = AVDISCARD_DEFAULT;  // 恢复正常解码
```

预期效果：seek 解码帧数减少 50-70%，延迟从 ~500ms 降至 ~150-200ms。改动极小（两行代码），可立即实施。

---

## 3. 解码器层面未做帧丢弃，目标前帧仍入队流转

> **已解决**：`DecoderNode::DropForSeek` 在解码线程按目标 PTS 丢帧，不进入链路。

### 问题

seek 后 decoder 解码出的所有帧（包括 pts < seek_target 的参考帧）都经过 `FrameQueue::Push` → video render loop `Pop` → 检查 `seek_target_` → 丢弃。虽然 profile 显示队列开销本身不大，但这些帧仍占据 frame_queue 容量（max_size=4），可能短暂阻塞 decoder 线程等待 render loop 消费。

### 影响场景

- **frame_queue 满时的阻塞**：decoder 解码出帧后被 Push 阻塞，等待 render loop pop 并丢弃
- **render loop 有 sleep 逻辑**：如果 sync delay 计算不当，丢帧速度受限

### 改进建议（参考 MPV `mp_decoder_wrapper_set_start_pts`）

在 Decoder 层面设置 `drop_until_pts_`，解码出的帧如果 pts < 目标值，直接丢弃不入队：

```cpp
void Decoder::SetDropUntilPts(double pts) {
    drop_until_pts_.store(pts, std::memory_order_release);
}

// DecodeLoop 中
AVFramePtr frame;
int ret = avcodec_receive_frame(codec_ctx_, frame.get());
if (ret < 0) break;

double pts = frame->pts * av_q2d(stream_->time_base);
double drop_target = drop_until_pts_.load(std::memory_order_acquire);
if (drop_target > 0 && pts < drop_target - 0.001) {
    continue;  // 不入队，直接丢弃
}
drop_until_pts_.store(-1.0);  // 清除标记
frame_queue_->Push(SerialFrame{std::move(frame), pkt_serial, false});
```

配合第 2 项 `skip_frame` 使用效果更佳：skip_frame 减少解码量，drop_until_pts 减少队列流转量。

---

## 4. Seek 后第一帧未跳过 A/V sync delay

### 问题

seek 完成后 video render loop 拿到第一帧仍执行 `ComputeDisplayDelay()` 计算 A/V 同步延迟。此时 audio clock 可能尚未更新到正确位置，导致计算出一个正的 delay，白白等待一段时间才显示。

### 影响场景

- **任何 seek 操作**：即使帧已经解码就绪，用户仍需等待 sync delay（10-50ms）
- **纯视频文件（VideoMaster 模式）**：影响较小但仍有不必要的 `last_pts` 与 `frame_timer_` 重置开销

### 改进建议（参考 MPV `restart_complete` 逻辑）

seek 后设置一个 `seek_just_done_` 标记，video render loop 遇到第一帧时跳过 delay：

```cpp
if (seek_just_done_) {
    seek_just_done_ = false;
    frame_timer_ = Clock::Now();  // 重置 frame_timer 基准
    // delay = 0, 立即渲染
} else {
    double delay = ComputeDisplayDelay(pts, last_pts, last_display_time);
    if (delay > 0) sleep(delay);
}
```

---

## 5. 缺少 Seek 请求合并机制

### 问题

用户快速拖动进度条时，每次 slider 值变化都触发一次完整 seek（Flush + avformat_seek_file + decode→render 全链路），大量中间 seek 请求被浪费。

### 影响场景

- **进度条拖动**：用户从 10s 拖到 60s，途中可能触发 20+ 次 seek，每次都执行完整 I/O
- **键盘连续按方向键**：快速按 5 次右箭头（各 5s），理想行为是只 seek 到 +25s 位置

### 改进建议（参考 MPV `queue_seek` + `execute_queued_seek`）

```cpp
// Player 层面
void PlayerImpl::Seek(double position_seconds) {
    pending_seek_target_.store(position_seconds);
    seek_pending_.store(true);
    // 不立即执行，由 render loop 或定时器触发
}

// VideoRenderLoop 每帧检查
if (seek_pending_.load()) {
    double target = pending_seek_target_.load();
    seek_pending_.store(false);
    ExecuteSeek(target);  // 实际执行
}
```

MPV 还有 0.3s 的 delay 机制：如果上一次 seek 后还没显示出帧，就等一帧显示后再处理下一个 seek。

---

## 6. 无 Demuxer 缓存，每次 Seek 都做磁盘 I/O

### 问题

每次 seek 都调用 `av_seek_frame()` 执行实际文件 I/O。即使 seek 目标在当前已读取范围内（如前后几秒），仍然重新从磁盘读取。

### 影响场景

- **短距离 seek（±2-5s）**：文件 I/O 耗时 10-100ms，实际上这些 packet 刚刚被读过
- **网络流/慢速磁盘**：I/O 延迟更大，直接决定 seek 速度下限
- **来回反复 seek**：在同一段视频区间来回拖动，每次都重新读取

### 改进建议（参考 MPV `demux/demux.c` 的 packet cache）

维护已读 packet 的环形缓存，seek 时先检查目标是否在缓存范围内：

```cpp
class DemuxCache {
    // 按 DTS 排序的 packet 环形缓冲，保留最近 N 秒数据
    std::deque<CachedPacket> cache_;
    double cache_start_pts_;
    double cache_end_pts_;

public:
    bool SeekInCache(double target_pts) {
        if (target_pts >= cache_start_pts_ && target_pts <= cache_end_pts_) {
            // 直接从 cache 重新投递 packet 到 queue
            ReplayFromCache(target_pts);
            return true;  // 无需文件 I/O
        }
        return false;  // 回退到 av_seek_frame
    }
};
```

这是 MPV seek 速度的核心优势之一，但实现复杂度较高（需管理缓存淘汰、多流同步等）。

---

## 7. VO 层 Seek 期间无旧帧保持

### 问题

seek 时 `FrameQueue::Flush()` 清空队列，video render loop 在拿到新帧前可能短暂无内容可渲染。虽然 SDL texture 仍保留上一帧内容（不会黑屏），但 render loop 可能在空队列上阻塞等待，导致窗口 resize / OSD 更新等无法响应。

### 影响场景

- **seek 期间窗口拖动/缩放**：renderer 线程卡在 `Pop()` 等待中，无法处理 resize 事件
- **未来加 OSD（进度条、时间戳）**：seek 期间 OSD 也无法刷新

### 改进建议（参考 MPV `vo_has_frame` / `vo_redraw`）

VO 层保存"当前显示帧"的引用，即使队列空了也能响应 redraw 请求：

```cpp
class VideoRenderer {
    VideoFrame last_displayed_frame_;  // 始终保留最后一帧

    void Render(const VideoFrame& frame) {
        // ... 正常渲染 ...
        last_displayed_frame_ = frame;  // 保存
    }

    void Redraw() {
        // seek 期间 / resize 时调用
        if (last_displayed_frame_.Valid())
            RenderTexture(last_displayed_frame_);
    }
};
```

---

## 8. 渲染管线缺少全链路 GPU 零拷贝架构

> **已解决（2026-09-30，change gpu-stage-a-hw-decode-render）**：解码线程 blit 到呈现纹理池，SDL3 外部纹理绑定呈现；设备由图协商注入而非 HWAccelContext。

### 问题

当前视频帧从 Decoder 到显示经历：AVFrame (CPU YUV) → FrameConverter → SDL_UpdateYUVTexture (CPU→GPU 上传) → SDL_RenderTexture。即使未来启用硬解，若简单做 `av_hwframe_transfer_data`（GPU→CPU 拷贝再上传），4K NV12 帧 ~12MB，GPU→CPU→GPU 往返 2-3ms/帧，60fps 下占 12-18% 帧预算，抵消硬解收益。

### 影响场景

- **硬解启用后若用 transfer**：GPU 解码 → CPU 拷贝 → SDL_UpdateTexture 再上传 GPU = 两次跨总线传输
- **4K 60fps**：帧预算仅 16.6ms，2-3ms 的拷贝不可接受
- **未来多路视频/滤镜**：每一步额外拷贝都是性能瓶颈

### 改进建议（参考 MPV `vo_gpu` / VLC `d3d11_vout_display`）

保持 SDL3 作为视频渲染后端，利用 SDL3 D3D11 后端原生支持 texture 直接导入实现零拷贝：

```
GPU 模式（零拷贝）：
  Decoder (D3D11VA) → AVFrame(D3D11 texture) → FrameQueue
    → VideoRenderer: SDL_CreateTextureWithProperties 直接绑定 D3D11 texture
    → SDL_RenderTexture（全程 GPU，无跨总线拷贝）

CPU 模式（当前，1 次拷贝）：
  Decoder (soft) → AVFrame(YUV420P) → FrameQueue
    → VideoRenderer: SDL_UpdateYUVTexture 上传（唯一一次 CPU→GPU 拷贝）
    → SDL_RenderTexture
```

**SDL3 零拷贝关键 API：**

```cpp
// D3D11VA 解码帧直接作为 SDL texture 渲染
SDL_PropertiesID props = SDL_CreateProperties();
SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, width);
SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, height);
SDL_SetNumberProperty(props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_NV12);
SDL_SetPointerProperty(props, SDL_PROP_TEXTURE_CREATE_D3D11_TEXTURE_POINTER,
                       (ID3D11Texture2D*)frame->data[0]);
SDL_Texture* tex = SDL_CreateTextureWithProperties(renderer, props);
SDL_RenderTexture(renderer, tex, nullptr, nullptr);
```

**架构分层：**

```cpp
// 硬件加速设备管理（单一职责）
class HWAccelContext {
public:
    static std::unique_ptr<HWAccelContext> Create(AVHWDeviceType type);
    AVBufferRef* DeviceRef() const;
    AVPixelFormat HWPixelFormat() const;
};

// Decoder 不变 — Open 时可选注入 HWAccelContext
bool Decoder::Open(AVStream* stream, HWAccelContext* hw_ctx = nullptr);

// VideoRenderer（已有）— 内部根据帧格式自适应渲染路径
class VideoRenderer {
    void Render(AVFrame* frame) {
        if (frame->format == AV_PIX_FMT_D3D11) {
            RenderHWFrame(frame);   // 零拷贝：D3D11 texture → SDL texture
        } else {
            RenderSWFrame(frame);   // 当前路径：SDL_UpdateYUVTexture
        }
    }
};
```

**模式切换**：Player 初始化时根据配置创建 HWAccelContext 注入 Decoder，VideoRenderer 根据 `frame->format` 自适应选择渲染路径。对外接口不变。

**预期收益**：
- GPU 模式：解码+渲染全程零拷贝，4K 60fps 无压力
- CPU 模式：维持现状，仅 SDL_UpdateYUVTexture 一次上传（已是最优）
- 无需引入 OpenGL —— SDL3 D3D11 后端原生覆盖零拷贝需求

---

## 9. 拖动进度条时每次都做精确 seek（硬解后的主要剩余延迟）

> 记录时间：2026-09-30，数据来自 4K 60fps H.264（bbb_sunflower_2160p_60fps）硬解实测

### 问题

`QSlider::sliderMoved` 与 `sliderReleased` 都路由到 `PlayerPage::OnSliderMoved` → `MediaPlayer::Seek`，走的都是精确 seek：demux 回到目标前最近的关键帧（`AVSEEK_FLAG_BACKWARD`），解码器从关键帧一路解到目标 PTS 再显示。硬解吞吐约 240 帧/秒，延迟完全取决于目标离关键帧多远：

| seek 目标 | 请求 → 首帧 |
|---|---|
| 399.14s | 0.02s |
| 194.18s | 0.06s |
| 343.94s | 0.30s |
| 111.05s | 0.62s |
| 316.65s | 0.97s |

### 影响场景

- **拖动进度条**：拖动期间画面停在旧帧，要等最后一次精确 seek 追完才更新，体感「不是秒切」
- **长 GOP 片源**：关键帧间隔越大，最坏延迟越接近「GOP 长度 / 解码帧率」
- **连续拖动**：每次中间 seek 都会触发 flush + FFmpeg 重建硬件解码器（见第 10 项），全部被下一次 seek 作废

### 改进建议（参考 MPV OSC：拖动中 keyframe seek，松手 exact seek）

MPV 的 OSC 在拖动时发 `seek <pos> absolute+keyframes`，松手后才发 `absolute+exact`；FFplay 默认只做关键帧 seek。把 seek 精度提升为接口参数，由 UI 按交互阶段选择：

```cpp
enum class SeekMode { kExact, kKeyframe };
void MediaPlayer::Seek(double pos, SeekMode mode);

// DecoderNode::OnCommand(kSeek)
drop_until_pts_ = (cmd.mode == SeekMode::kExact) ? cmd.position : 0.0;  // keyframe: 不追帧

// PlayerPage：拆开两个信号
sliderMoved    -> player_->Seek(pos, SeekMode::kKeyframe);  // 拖动中：秒切到关键帧
sliderReleased -> player_->Seek(pos, SeekMode::kExact);     // 松手：精确到目标
```

注意 keyframe 模式下音频时钟要以 demux 实际落点（首个视频关键帧 PTS）为准重置，而不是以请求位置重置，否则 A/V 会短暂失步。

---

## 10. 每次 seek 都触发 FFmpeg 重建硬件解码器与 surface 池

### 问题

h264 解码器 `avcodec_flush_buffers` 后 `context_initialized=0`，下一个关键帧重走 `get_format`：FFmpeg 拆掉旧 hwaccel（释放 `ID3D11VideoDecoder`），丢弃旧 `hw_frames_ctx`，新建解码器与 20 个 surface 的纹理数组。日志中每次 seek 都有一条 `hw frames context replaced`。旧纹理数组要等最后一个引用它的帧释放才销毁（正是 seek 冻结事故的触发点，见 gpu-stage-a design §5.1）。

### 影响场景

- **每次 seek 多一次 GPU 资源分配**：4K 下 20 张 NV12 surface 约 250MB 的分配与释放，seek 后首个关键帧的 `send_packet` 明显变慢（实测 10ms+）
- **拖动进度条**：连续 seek 反复分配/释放显存

### 改进建议（参考 FFmpeg `get_format` 文档约定 + MPV `hwdec_extra_frames`）

在 `DecoderNode::GetFormat` 回调里自行提供 `hw_frames_ctx`：首次用 `avcodec_get_hw_frames_parameters()`（必须在 get_format 内调用，open2 之前调用会崩溃）生成参数并缓存；后续 get_format 若尺寸/格式未变则直接 `av_buffer_ref` 复用缓存的帧上下文。额外 surface 数用 `codec_ctx_->extra_hw_frames` 声明（MPV 默认 6），不要手工拼 `AVHWFramesContext`（缺 `D3D11_BIND_DECODER`、高度未按 16 对齐会报 80070057）。

---

## 11. 硬解 seek 追赶未跳过非参考帧

### 问题

`DecoderNode::MaybeFlushOnSerialChange` 只对软解设置 `AVDISCARD_NONREF`，硬解追赶期解码所有帧。原先排除硬解的理由（「丢弃输出导致 surface 泄漏、send_packet 永久阻塞」）已被证伪：冻结的真实根因是 SDL 创建的单线程 D3D11 设备（gpu-stage-a design §5.1）。NONREF 与 D3D11VA 的组合至今未验证。

### 影响场景

- **硬解长 GOP seek**：B 帧占比 50%+ 时，追赶阶段有一半解码是浪费，直接决定第 9 项表格中 0.5s 以上的延迟

### 改进建议（参考 MPV `hr-seek-framedrop`）

对硬解同样在追赶期设置 `AVDISCARD_NONREF`，到达目标后恢复；验证点：连续拖动 20 次以上 surface 占用（日志 surface 索引）不增长、无 FFmpeg 报错、目标帧若为非参考 B 帧时首帧落点可接受（最多晚一个 B 帧）。
