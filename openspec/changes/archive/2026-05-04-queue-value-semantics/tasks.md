## 1. PacketQueue 接口改造

- [x] 1.1 `packet_queue.h`：将 `SerialPacket` 从 private 移至 public，添加 `#include <optional>`
- [x] 1.2 `packet_queue.h`：`Push` 签名改为 `void Push(SerialPacket sp)`
- [x] 1.3 `packet_queue.h`：`Pop` 签名改为 `std::optional<SerialPacket> Pop()`
- [x] 1.4 `packet_queue.cc`：Push 实现直接 `std::move(sp)` 入队，移除 `av_packet_move_ref`
- [x] 1.5 `packet_queue.cc`：Pop 实现返回 `std::optional`，移除 `av_packet_move_ref`

## 2. FrameQueue 接口改造

- [x] 2.1 `frame_queue.h`：将 `SerialFrame` 从 private 移至 public，添加 `#include <optional>`
- [x] 2.2 `frame_queue.h`：`Push` 签名改为 `void Push(SerialFrame sf)`
- [x] 2.3 `frame_queue.h`：`Pop` 签名改为 `std::optional<SerialFrame> Pop()`
- [x] 2.4 `frame_queue.cc`：Push 实现直接 `std::move(sf)` 入队，移除 `av_frame_ref`/`av_frame_move_ref`
- [x] 2.5 `frame_queue.cc`：Pop 实现返回 `std::optional`，移除 `av_frame_move_ref`
- [x] 2.6 `frame_queue.h/cc`：`PushEof` 保持便捷接口，内部构造 `SerialFrame{AVFramePtr{}, serial, true}`

## 3. 调用方适配

- [x] 3.1 `demuxer.cc`：Push 调用改为构造 `SerialPacket{std::move(pkt), serial}`
- [x] 3.2 `decoder.cc`：Pop 调用改为 `auto sp = packet_queue_->Pop(); if (!sp) break;`
- [x] 3.3 `decoder.cc`：EnqueueFrame 中 `frame_queue_->Push` 改为构造 `SerialFrame`
- [x] 3.4 `audio_renderer.cc`：Pop 调用改为 `auto sf = frame_queue_->Pop(); if (!sf) break;`
- [x] 3.5 `player.cc`：VideoRenderLoop 中 Pop 调用适配新接口

## 4. 规范同步与验证

- [x] 4.1 更新 `openspec/specs/demux-decode/spec.md` 中 PacketQueue 接口描述
- [x] 4.2 编译通过，播放视频验证功能正常
