# 解码器选择待改进点

> 记录时间：2026-10-25
> 对标参考：mpv (`--hwdec` 与 `--vd` 的解码器选择)、FFmpeg (`avcodec_find_decoder` / `avcodec_find_decoder_by_name`)
> 关联：change player-robustness-and-debug-settings 的真实素材测试

---

## 1. AV1 默认选中 libdav1d，硬件解码永远不会启用

### 问题

`DecoderNode::FindAndOpenCodec` 用 `avcodec_find_decoder(codec_id)` 取解码器。FFmpeg 7.1 的 full build 同时注册了 `libdav1d` 与原生 `av1` 两个 AV1 解码器，`avcodec_find_decoder` 返回先注册的 `libdav1d`；而 hwaccel（D3D11VA/DXVA2/NVDEC 等）只挂在原生 `av1` 解码器上，`libdav1d` 没有 `hw_configs`。`GpuDevice::SupportsDecoder(codec)` 因此判定不支持，AV1 永远走 CPU。

真实素材测试的日志：

```
DecoderNode(video): software decode (libdav1d)
```

### 影响场景

- **支持 AV1 硬解的显卡**（NVIDIA RTX 30+、Intel Xe、AMD RDNA2+）：4K AV1 本可零拷贝硬解，实际占满 CPU，seek 追赶也慢
- **设置页「硬件解码」开关**：对 AV1 开与不开没有区别，诊断区始终显示 CPU

### 改进建议（参考 mpv 的解码器选择）

mpv 先按 `--hwdec` 的需要挑「支持该 hwaccel 的解码器」，没有再回退到默认软件解码器。对应到本项目：允许硬件解码时，在同一 `codec_id` 的所有解码器中优先选择带当前设备类型 `hw_config` 的那个，打开失败再用默认解码器：

```cpp
const AVCodec* PickDecoder(AVCodecID id, const gpu::GpuDevice* device) {
    if (device) {
        void* it = nullptr;
        while (const AVCodec* c = av_codec_iterate(&it)) {
            if (av_codec_is_decoder(c) && c->id == id && device->SupportsDecoder(c)) {
                return c;  // e.g. native "av1" with D3D11VA
            }
        }
    }
    return avcodec_find_decoder(id);  // libdav1d stays the software choice
}
```

注意软件路径仍应优先 `libdav1d`（比原生 `av1` 软解快得多），所以只在要硬解时改变选择。
