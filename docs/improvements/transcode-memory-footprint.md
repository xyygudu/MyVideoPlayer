# 转码内存占用待改进点

> 记录时间：2026-08-02
> 对标参考：FFmpeg CLI (fftools/ffmpeg_enc.c)、x264
> 关联讨论：change link-capacity-must-be-explicit 的隔离测量（design D6）

---

## 1. 4K 转码峰值内存 3.4GB，主要来自编码器内部而非管线队列

### 问题

修复无界链路后，4K60（`bbb_sunflower_2160p_60fps`）转码峰值仍有 3407MB。隔离测量表明这部分**不在我们的队列里**：

| 测量 | 结果 |
|---|---|
| 4K60 修复前 | 3733 MB |
| 4K60 修复后 | 3407 MB |
| 无界链路的实际堆积 | 326 MB（约 26 帧） |
| 480p 修复后 | 191 MB (WS) / 225 MB (private) |

关键证据：**4K/480p 内存比 = 17.8×，而像素面积比 = 20.2×**。内存与帧面积成正比、持有帧数恒定在约 300 —— 而本项目的链路修复后最多只缓 4 帧。

这约 300 帧位于：

- **libx264**：日志显示 `threads=15 lookahead_threads=2 rc_lookahead=40`。光是前瞻窗口 40 帧 × 15 个编码线程各自持有的帧，就是几十到上百帧。
- **libavcodec 解码器**：帧级多线程（`thread_count` 默认为核数）加上 H.264 DPB，又是几十帧。

当前 `EncoderNode` 对这两组参数**完全不设置**，一律使用 FFmpeg/x264 默认值。默认值是为吞吐调优的，对内存没有任何约束。

### 影响场景

- **4K / 8K 转码**：8K 单帧约 99MB，同样约 300 帧驻留意味着峰值可达 30GB 量级，普通机器直接 OOM
- **多任务并发**：将来若支持同时转码多个文件，每个任务各吃 3.4GB，两三个任务就撑满 16GB 内存
- **无诊断手段**：目前无法回答"这次转码会占多少内存"，也没有任何参数可调

### 改进建议（参考 FFmpeg CLI）

FFmpeg CLI 把这些暴露为可配置项而非硬编码默认：

- `-threads N` → `AVCodecContext::thread_count`
- `-x264-params rc-lookahead=N` → 经 `AVDictionary` 透传给 libx264

对应到本项目，应把它们提升为 `EncodeParams` 的字段，由建图方按分辨率决策：

```cpp
struct EncodeParams {
    // ...
    int  thread_count{0};        // 0 = FFmpeg 自动
    int  rc_lookahead{-1};       // -1 = 编码器默认
};

// EncoderNode::OpenCodec()
if (params_.thread_count > 0) codec_ctx_->thread_count = params_.thread_count;
if (params_.rc_lookahead >= 0) {
    av_dict_set_int(&opts, "rc-lookahead", params_.rc_lookahead, 0);
}
```

选择策略可参考 `分辨率 → 上限` 的简单映射（在 Transcoder 门面里决策，不下沉到 EncoderNode）：4K 及以上收紧 `rc_lookahead`，以少量码率效率换取可预测的内存上限。

**注意权衡**：`rc_lookahead` 直接影响 x264 的码率分配质量，调小会损失压缩效率；`thread_count` 调小会损失吞吐。因此这必须是**用户可见的选项**，不能由我们静默替用户决定。
