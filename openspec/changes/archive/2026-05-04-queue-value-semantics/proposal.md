## Why

PacketQueue 和 FrameQueue 的 Push/Pop 接口使用裸指针 + out 参数的 C 风格设计，需要手动调用 `av_packet_move_ref` 做数据搬运，代码可读性差且容易遗漏生命周期管理。改为 C++ 值语义接口（move `AVPacketPtr`/`AVFramePtr`）可消除所有手动 move_ref 调用，提升可维护性，无性能损失。

## What Changes

- **BREAKING** `PacketQueue::Push` 签名从 `Push(AVPacket*, int)` 改为 `Push(SerialPacket)` 接收值语义结构体
- **BREAKING** `PacketQueue::Pop` 签名从 `bool Pop(AVPacket*, int*)` 改为 `std::optional<SerialPacket> Pop()`
- **BREAKING** `FrameQueue::Push` 签名从 `Push(AVFrame*, int)` 改为 `Push(SerialFrame)`
- **BREAKING** `FrameQueue::Pop` 签名从 `bool Pop(AVFrame*, int*, bool*)` 改为 `std::optional<SerialFrame> Pop()`
- `SerialPacket` 和 `SerialFrame` 从 private 提升为 public
- 移除 Push/Pop 内部的 `av_packet_move_ref` / `av_frame_move_ref` 调用
- 调用方（Demuxer、Decoder、AudioRenderer、PlayerImpl）适配新接口

## Capabilities

### New Capabilities

（无新能力，纯接口重构）

### Modified Capabilities

- `demux-decode`: PacketQueue/FrameQueue 的 Push/Pop 接口签名变更，Pop 返回值方式从 out 参数改为 optional 返回

## Impact

- `src/core/src/packet_queue.h/cc` — 接口变更
- `src/core/src/frame_queue.h/cc` — 接口变更
- `src/core/src/demuxer.cc` — Push 调用适配
- `src/core/src/decoder.cc` — Pop 调用适配
- `src/core/src/audio_renderer.cc` — Pop 调用适配
- `src/core/src/player.cc` — VideoRenderLoop 中 Pop 调用适配
- `openspec/specs/demux-decode/spec.md` — 更新接口描述
