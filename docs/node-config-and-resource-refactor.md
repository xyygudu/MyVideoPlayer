# 节点配置与资源管理架构重构方案

> 状态：**待评审**（经同意后实施）
> 关联：openspec/changes/media-graph-architecture
> 日期：2026-06-22

## 1. 背景与动机

当前 MediaGraph 节点图架构已完成播放功能，但在节点的「配置 / 参数 / 资源」管理上存在三处耦合问题，会阻碍未来向**转码、录屏、摄像头采集、推拉流**等场景扩展。三个问题表面独立，实则指向同一个根因：**没有按本质区分三类不同性质的数据**。

### 1.1 三个具体问题

| # | 问题 | 位置 |
|---|------|------|
| P1 | `NodeConfig` 是「上帝结构体」，包含所有节点的字段，违反接口隔离原则 | node.h |
| P2 | `DemuxNode` 和 `MediaPlayer::BuildGraph` 各执行一次 `avformat_open_input`，重复打开文件 | media_player.cc |
| P3 | `hw_accel_` 仅视频解码器使用，但创建逻辑散落在 MediaPlayer 中，未来硬编场景无法共享设备 | media_player.cc |

### 1.2 统一根因：三类数据混淆

业界成熟框架（尤其 GStreamer）将节点相关数据严格分为三类，分别用不同机制承载：

| 类别 | 例子 | 正确归属 | 对应问题 |
|------|------|----------|----------|
| **静态配置** | file_path, codec_name, bitrate | 节点构造参数（强类型） | P1 |
| **动态流元数据** | codec 参数、分辨率、采样率 | 顺着 Port/Link 协商流动 | P2 |
| **共享资源** | Clock、HW 设备 | Graph 级，注入 | P3 |

对应 GStreamer 的三层模型：**element 属性 / pad caps / pipeline context**。本方案即按此三层模型重构。

---

## 2. 业界做法分析

### 2.1 配置（P1）

| 框架 | 配置方式 | 本质 |
|------|----------|------|
| GStreamer | `g_object_set(element, "bitrate", 4000, NULL)` | 每个 element 独立的 GObject 属性 |
| FFmpeg AVOptions | `av_opt_set(ctx, "preset", "fast")` | 每个 context 独立的 option 集 |
| OBS | `obs_data_t`（JSON 字典） | 每个 source/output 独立的动态字典 |
| DirectShow | 各 filter 独立的 COM 接口 | 强类型，每 filter 独立 |

**结论**：无任何框架用「一个大 config 喂所有节点」。配置本质是节点专属的。

### 2.2 流元数据协商（P2）

> **GStreamer**：demuxer 的 source pad 携带 caps（codec、分辨率、codec_data）。`demuxer ! decoder` 连接时，decoder 从 pad 的 caps 自我配置。**流参数顺着连接走，不走侧门。**

当前 `INode::Negotiate()` 是空壳 stub，本应承担此职责却未实现，导致 MediaPlayer 被迫重新打开文件取参数。

### 2.3 共享资源（P3）

| 框架 | HW 设备归属 |
|------|------------|
| GStreamer | `GstD3D11Device` 通过 `GstContext` 在 pipeline 级共享 |
| FFmpeg | `AVHWDeviceContext` 创建一次，`av_buffer_ref` 进多个 codec |

**结论**：HW 设备是管线级共享资源，非某节点私产。转码场景「硬解 + 硬编」需共享同一 D3D11 设备以实现 GPU 内零拷贝。

---

## 3. 设计决策

### 决策 1：移除 NodeConfig，配置改为节点专属强类型

**选择**：从 `INode` 接口移除 `Configure(NodeConfig)`，配置通过节点构造参数或专属 Config 注入。
**替代方案**：保留 NodeConfig 但拆成基类 + 派生（dynamic_cast 下行转换）—— 否决，类型不安全且脆弱。

**理由**：配置是节点专属的，`MediaGraph` 无需多态配置节点——**建图者（MediaPlayer/Transcoder）知道自己创建的具体类型**。只有「生命周期 + 数据流」需要多态。这正是 GStreamer 模型：pipeline 只多态管理状态切换，属性设置是具体 element 的事。

### 决策 2：流参数通过 MediaFormat 协商流动，存储为拷贝

**选择**：MediaFormat 携带 `shared_ptr<AVCodecParameters>`（深拷贝 + 自定义 deleter），通过端口在 Negotiate 阶段流动。
**替代方案 A**：MediaFormat 存 `AVStream*` 引用 —— 否决。
**替代方案 B**：MediaFormat 深拷贝、每次拷贝 MediaFormat 都复制参数 —— 否决，浪费。

**为何拷贝而非引用**（决定性理由）：

1. **生命周期安全**：`AVStream*` 归 DemuxNode 的 `format_ctx_` 所有，要求 DemuxNode 严格晚于所有解码器析构。但 `vector<unique_ptr<INode>>` 析构顺序标准未保证，DemuxNode 作为首个 AddNode 很可能先析构 → 悬空指针。
2. **拷贝成本可忽略**：`avcodec_parameters_copy` 拷小结构体 + extradata（几百字节），**整个生命周期只在建图时一次**，非每帧。
3. **决定性：未来非文件源没有 AVStream**：

| 源类型 | 有 AVStream |
|--------|-------------|
| 文件 Demux / 网络拉流 | ✅ |
| 摄像头采集 | ❌ |
| 屏幕录制 | ❌ |
| 编码器输出（喂 Mux） | ❌（只有 AVCodecContext） |

若存 `AVStream*`，则只有文件/网络源能填充格式，摄像头/录屏节点无法参与同一协商机制，**摧毁图架构「统一协商」的核心价值**。存拷贝则任何源都能填：文件从 codecpar 拷、摄像头从设备能力构造、编码器从自己的 codec context 填。

**为何 shared_ptr**：MediaFormat 在端口 get/set 间会多次拷贝（现有 `SetFormat(MediaFormat)` 是值传递）。`shared_ptr` 让 MediaFormat 保持廉价可拷贝 + 生命周期自管理，无需把 MediaFormat 改成 move-only 牵连所有端口代码。

### 决策 3：HW 设备提升为 Graph 级共享资源

**选择**：`HWAccelContext` 从 MediaPlayer 成员提升为 MediaGraph 共享资源（与 Clock 平行），需要的节点从 graph 查询。
**替代方案**：搬进 DecoderNode 内部 —— 否决，未来转码「硬解 + 硬编共享设备零拷贝」无法实现。

**理由**：HW 设备是潜在多节点共享资源（解码器 + 编码器）。与 design.md Decision 5（Clock 为 graph 全局）保持对称。

---

## 4. 详细代码变更

### 4.1 MediaFormat 扩展（决策 2 核心）

**文件**：`src/core/src/graph/media_format.h` / `.cc`

新增 `shared_ptr<AVCodecParameters>` 成员与 Stream 工厂方法：

```cpp
// media_format.h —— 新增
extern "C" {
#include <libavcodec/codec_par.h>
}

class MediaFormat {
  public:
    // 新增工厂：从 AVStream 的 codecpar 深拷贝构造（用于 Demux 输出端口）
    static MediaFormat FromStream(int codec_id, Rational time_base,
                                  Rational frame_rate,
                                  const AVCodecParameters* codecpar,
                                  MediaType type);

    // 新增访问器
    const AVCodecParameters* codec_params() const { return codec_params_.get(); }

  private:
    // 新增成员：编码参数的共享拷贝（自定义 deleter）
    std::shared_ptr<AVCodecParameters> codec_params_;
};
```

```cpp
// media_format.cc —— 新增实现
MediaFormat MediaFormat::FromStream(int codec_id, Rational time_base,
                                    Rational frame_rate,
                                    const AVCodecParameters* codecpar,
                                    MediaType type) {
    MediaFormat f;
    f.media_type_ = type;
    f.codec_id_ = codec_id;
    f.time_base_ = time_base;
    f.frame_rate_ = frame_rate;
    if (codecpar) {
        AVCodecParameters* p = avcodec_parameters_alloc();
        avcodec_parameters_copy(p, codecpar);
        f.codec_params_ = std::shared_ptr<AVCodecParameters>(
            p, [](AVCodecParameters* x) { avcodec_parameters_free(&x); });
    }
    return f;
}
```

**变更理由**：让流参数自包含、可顺着端口流动，且生命周期独立于 DemuxNode。

### 4.2 DemuxNode：输出端口携带完整流参数

**文件**：`src/core/src/nodes/demux_node.cc`（Prepare 内）

```cpp
// 旧：仅携带 codec_id + time_base
port->SetFormat(MediaFormat::Packet(stream->codecpar->codec_id, tb));

// 新：携带完整 codecpar 拷贝 + frame_rate
Rational fr{stream->avg_frame_rate.num, stream->avg_frame_rate.den};
port->SetFormat(MediaFormat::FromStream(
    stream->codecpar->codec_id, tb, fr, stream->codecpar,
    (是视频 ? MediaType::kVideo : MediaType::kAudio)));
```

**变更理由**：DemuxNode 是唯一打开文件的地方，把下游所需的全部参数放进输出端口的 MediaFormat。

### 4.3 DecoderNode：从输入端口协商获取参数，移除 SetStream

**文件**：`src/core/src/nodes/decoder_node.h` / `.cc`

```cpp
// decoder_node.h —— 删除
- void SetStream(AVStream* stream);   // 删除
- AVStream* stream_{nullptr};         // 删除

// Negotiate 改为真正实现（旧为空壳）
bool DecoderNode::Negotiate() {
    // 从输入端口读取上游协商的 MediaFormat
    const MediaFormat& fmt = input_port_->Format();
    const AVCodecParameters* codecpar = fmt.codec_params();
    if (!codecpar) {
        SPDLOG_ERROR("DecoderNode: no codec params from upstream");
        return false;
    }
    // 缓存供 Prepare 使用
    negotiated_codecpar_ = codecpar;   // const 指针，shared_ptr 由 MediaFormat 持有
    time_base_ = { fmt.time_base().num, fmt.time_base().den };
    return true;
}

// Prepare 用 negotiated_codecpar_ 替代 stream_->codecpar
bool DecoderNode::Prepare() {
    const AVCodec* codec = avcodec_find_decoder(negotiated_codecpar_->codec_id);
    codec_ctx_ = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx_, negotiated_codecpar_);
    // ... HW accel、open2 不变
}
```

**关键前提**：MediaGraph 必须在 `Negotiate()` 阶段把上游 OutputPort 的 Format 传播到下游 InputPort（见 4.6）。

**变更理由**：解码器自我配置于协商格式，不再依赖外部 SetStream，也不需要 AVStream*。

### 4.4 InputPort 需暴露协商后的 Format

**文件**：`src/core/src/graph/port.h`（已有 `Format()` 访问器，确认 Connect 时传播）

```cpp
// port.cc —— OutputPort::Connect 中补充：把输出格式同步给输入端口
bool OutputPort::Connect(InputPort* peer, int link_capacity) {
    // ... 现有连接逻辑
    peer->SetFormat(format_);   // 新增：传播协商格式到下游输入端口
    // ...
}
```

**变更理由**：让下游节点 Negotiate 时能从输入端口读到上游格式。

### 4.5 HW 设备提升为 Graph 共享资源（决策 3）

**文件**：`src/core/src/graph/media_graph.h`

```cpp
// 新增（与 Clock 平行）
#include "hw_accel_context.h"   // 或前向声明

class MediaGraph {
  public:
    void SetHWDevice(std::shared_ptr<mvp::HWAccelContext> hw) {
        hw_device_ = std::move(hw);
    }
    std::shared_ptr<mvp::HWAccelContext> HWDevice() const { return hw_device_; }
  private:
    std::shared_ptr<mvp::HWAccelContext> hw_device_;   // 新增
};
```

**DecoderNode** 改为从 graph 查询（需持有 graph 指针，已有 SetGraph 模式可复用）：

```cpp
// decoder_node：Prepare 中
if (media_type_ == MediaType::kVideo && graph_ && graph_->HWDevice()) {
    hw_ctx_ = graph_->HWDevice().get();
    // ... 现有 HW 绑定逻辑
}
```

**变更理由**：HW 设备成为可被解码器、未来编码器共享的 graph 级资源，支持转码零拷贝。

### 4.6 MediaGraph::Negotiate 传播格式

**文件**：`src/core/src/graph/media_graph.cc`

确认 Negotiate 按拓扑序执行，且每个节点 Negotiate 前其输入端口已被上游 Connect 填充格式。当前 `Connect` 在建图时调用（早于 Negotiate），故 4.4 的格式传播已能保证顺序正确。Negotiate 内逐节点调用 `node->Negotiate()` 即可（已有）。

### 4.7 MediaPlayer::BuildGraph 大幅简化（P1/P2/P3 收敛）

**文件**：`src/core/src/media_player.cc`

```cpp
// 删除：第二次 avformat_open_input
- AVFormatContext* fmt_ctx = nullptr;
- avformat_open_input(&fmt_ctx, filepath.c_str(), nullptr, nullptr);
- avformat_find_stream_info(fmt_ctx, nullptr);

// HW 设备改为注入 graph
hw_accel_ = HWAccelContext::Create(AV_HWDEVICE_TYPE_D3D11VA);
graph_->SetHWDevice(hw_accel_);   // 共享给需要的节点

// DecoderNode 创建简化：不再 SetStream/SetHWAccel
auto vdecoder = std::make_unique<graph::DecoderNode>();
video_decoder_ = static_cast<graph::DecoderNode*>(
    graph_->AddNode(std::move(vdecoder)));
// 仅连接，参数由 Negotiate 自动协商
graph_->Connect(demux_outputs[video_port_idx],
                video_decoder_->Inputs()[0], 256);
graph_->Connect(video_decoder_->Outputs()[0],
                video_sink_->Inputs()[0], 8);

// video_fps_ 改从端口 MediaFormat 读取（DemuxNode 已写入 frame_rate）
auto vfmt = demux_outputs[video_port_idx]->Format();
video_fps_ = vfmt.frame_rate().den > 0
    ? (double)vfmt.frame_rate().num / vfmt.frame_rate().den : 30.0;

// VideoSinkNode/AudioSinkNode 的 SetStream 也改为从端口格式读取
//（采样率/声道数从 codec_params 取，或新增 MediaFormat 音频字段）
```

**变更理由**：单次打开文件，参数协商自动流动，HW 设备共享注入，BuildGraph 显著瘦身。

### 4.8 移除 NodeConfig（决策 1）

**文件**：`src/core/src/graph/node.h` 及 4 个节点

```cpp
// node.h —— 删除整个 NodeConfig 结构体
- struct NodeConfig { ... };

// INode 接口移除 Configure
- virtual bool Configure(const NodeConfig& config) = 0;

// 各节点删除 Configure 实现；DemuxNode 文件路径改构造参数
class DemuxNode : public INode {
  public:
    explicit DemuxNode(std::string file_path);   // 路径构造注入
};
```

NodeState 中的 `kConfigured` 状态：保留但语义调整为「构造完成」，或直接从 Idle → Prepared（构造即配置）。建议保留状态枚举但 Prepare 入口同时接受 Idle/kConfigured，最小改动。

**变更理由**：配置回归节点专属强类型，消除上帝结构体。

---

## 5. 变更文件清单

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `graph/media_format.h/.cc` | 扩展 | 新增 shared_ptr<AVCodecParameters> + FromStream 工厂 |
| `graph/node.h` | 删除 | 移除 NodeConfig 与 Configure |
| `graph/port.h/.cc` | 微调 | Connect 时传播格式到下游输入端口 |
| `graph/media_graph.h/.cc` | 扩展 | 新增 HWDevice 共享资源 |
| `nodes/demux_node.h/.cc` | 重构 | 构造注入路径；输出端口携带完整参数；删除 Configure |
| `nodes/decoder_node.h/.cc` | 重构 | 删除 SetStream/SetHWAccel；Negotiate 真正实现；从端口/graph 取参数与 HW |
| `nodes/video_sink_node.h/.cc` | 微调 | 参数从端口格式读取；删除 Configure |
| `nodes/audio_sink_node.h/.cc` | 微调 | 参数从端口格式读取；删除 Configure/SetStream |
| `media_player.cc` | 简化 | 删除二次 open；HW 注入 graph；BuildGraph 瘦身 |

---

## 6. 实施顺序（保证每步可编译）

1. **MediaFormat 扩展**（4.1）—— 纯新增，不破坏现有。
2. **Port 格式传播**（4.4）—— Connect 补一行。
3. **DemuxNode 输出端口携带参数**（4.2）—— 填充新字段。
4. **DecoderNode 协商重构**（4.3）—— 改用端口参数，保留 SetStream 作为过渡（双轨）。
5. **HW 设备提升 graph**（4.5）—— 新增注入路径，保留 SetHWAccel 过渡。
6. **MediaPlayer 简化**（4.7）—— 删除二次 open，切换到协商路径。
7. **移除过渡代码**（4.3/4.5 的 SetStream/SetHWAccel）与 **NodeConfig**（4.8）—— 最后统一清理。
8. 全量构建 + 运行验证（播放、seek、音视频同步）。

---

## 7. 风险与缓解

| 风险 | 影响 | 缓解 |
|------|------|------|
| Negotiate 顺序依赖：下游 Negotiate 时上游格式必须已就绪 | 解码器拿不到参数 | Connect 在建图期（早于 Negotiate）即传播格式；MediaGraph::Negotiate 按拓扑序执行 |
| 音频 sink 需要采样率/声道，原从 AVStream 取 | sink 配置失败 | 从 codec_params（含 sample_rate/channels）读取，或在 MediaFormat 补音频字段 |
| AVCodecParameters 在内部头暴露 FFmpeg 类型 | 依赖方向 | media_format.h 已是内部头（src/graph/），允许 FFmpeg 类型 |
| NodeState::kConfigured 语义变化 | 状态机不一致 | Prepare 同时接受 Idle/kConfigured，最小改动 |

---

## 8. 未来扩展验证

按本方案重构后，三类未来场景均可纳入同一机制：

- **转码**：`Demux → Decoder → Encoder → Mux`。Encoder 输出端口携带编码后参数，Mux 在 Negotiate 读取。硬解硬编共享 graph 的 HWDevice 实现零拷贝。
- **摄像头/录屏**：CameraSourceNode 无 AVStream，但可在输出端口 MediaFormat 中**构造**裸帧参数（分辨率/像素格式），下游编码器照常协商。
- **推拉流**：RTMP 源/汇节点同样通过端口协商参数，与文件源对称。

**结论**：三层模型（构造配置 / 端口协商 / graph 共享资源）完整覆盖全部未来场景，无需再次破坏性重构。

---

## 9. 待确认问题

1. 音频参数（sample_rate/channels）是从 `codec_params` 取，还是在 MediaFormat 显式补 audio 字段？（建议后者，更清晰，sink 不必解析 codecpar）
2. NodeState 是否保留 `kConfigured`？（建议保留，Prepare 兼容两种入口）
3. 是否需要为 HWAccelContext 增加引用计数以支持多节点共享？（shared_ptr 已足够，FFmpeg 内部 av_buffer_ref 另算）
