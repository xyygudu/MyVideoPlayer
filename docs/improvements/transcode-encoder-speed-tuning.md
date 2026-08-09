# 转码编码器调速待改进点

> 记录时间：2026-08-09
> 对标参考：FFmpeg CLI（`-cpu-used`/`-deadline` for libvpx）
> 关联讨论：change container-codec-compatibility 上线后，UI 首次可选 libvpx-vp9 暴露的问题

---

## 1. EncodeParams/ApplyRateControl 只为 x264/x265 形状的编码器调过速，VP9/Opus 等用默认值跑

### 问题

`EncoderNode::ApplyRateControl()` 只设置这几个私有选项：

```cpp
av_dict_set_int(opts, "crf", params_.crf, 0);
av_dict_set(opts, "preset", params_.preset.c_str(), 0);  // x264/x265 专有概念
codec_ctx_->gop_size = params_.gop_size;
codec_ctx_->max_b_frames = params_.max_b_frames;
```

`"preset"`（`slow`/`medium`/`fast`）是 x264/x265 的私有选项名字，libvpx-vp9 没有同名选项。`avcodec_open2` 对无法识别的私有选项**静默丢弃、不报错**，因此选择 vp9 时，"画质预设"控件实际上什么都没传给编码器——libvpx 用自己的默认值 `deadline=good` + `cpu-used=0`（离线最高压缩效率档位，速度未经任何优化）。

`container-codec-compatibility` 变更之前，UI 编码器写死为 `libx264`/`aac`，这个缺口从未暴露；该变更把 vp9/opus/libmp3lame 从"不可选"变成"可选"后，用户选中 vp9 会遇到 1080p 转码极慢（deadline=good 下可能是 x264 的数十倍耗时），且没有任何提示或报错，观感等同于卡死。

### 影响场景

- **VP9**：`deadline`/`cpu-used` 完全未映射，默认跑最慢档位；用户以为程序卡死
- **Opus/MP3**：这两个音频编码器没有 `crf`/`preset` 概念，`ApplyRateControl` 现在对音频只设置 `bit_rate`，本身没错，但也没有为 Opus 暴露它真正的质量旋钮（如 `compression_level`/`vbr` 模式）
- **将来任何新增编码器**：只要不是 x264 系，大概率会重复这个"参数名对不上、静默无效"的问题

### 改进建议（参考 FFmpeg CLI）

FFmpeg CLI 用 `-cpu-used`/`-deadline`（vp8/vp9）而非 `-preset` 调速。改进方向是让 `EncodeParams`/`ApplyRateControl` 按编码器族分派，而不是假设所有编码器都认 `preset`：

```cpp
// EncodeParams 增加编码器无关的抽象字段，而非直接暴露 FFmpeg 私有选项名
struct EncodeParams {
    // ...
    int speed_effort{-1};  // -1 = 编码器默认；0(慢/高质量) ~ 8(快) 的抽象刻度
};

// ApplyRateControl 按编码器 id 分派，而非平铺 if-else 硬编码 "preset" 字符串
void EncoderNode::ApplyRateControl(AVDictionary** opts) {
    // ... 现有 crf/bitrate 逻辑不变
    switch (codec_->id) {
        case AV_CODEC_ID_H264:
        case AV_CODEC_ID_HEVC:
            if (!params_.preset.empty()) av_dict_set(opts, "preset", params_.preset.c_str(), 0);
            break;
        case AV_CODEC_ID_VP9:
            if (params_.speed_effort >= 0) {
                av_dict_set_int(opts, "cpu-used", params_.speed_effort, 0);
                av_dict_set(opts, "deadline", "good", 0);
            }
            break;
        default:
            break;  // 音频编码器等：目前无额外调速选项
    }
}
```

**注意权衡**：这不是"加个 if 分支"就能了事的补丁——一旦编码器族超过 2-3 种，应该考虑策略模式（每个编码器族一个 `IRateControlStrategy`），避免 `ApplyRateControl` 逐渐膨胀成分支堆叠。当前只有 vp9 一个新增族，暂不构建完整的策略模式抽象；如果后续再引入 av1/svt-av1 等，应在那次改动里把这里升级为策略分派。

**范围外**：UI 上"画质预设"三档（高质量/均衡/快速）目前的取值（crf+preset）也是照着 x264 设计的；一旦编码器可调速，这三档在不同编码器族下如何取值也需要重新设计，本条目只记录问题不展开方案。
