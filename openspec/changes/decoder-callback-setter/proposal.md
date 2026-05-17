## Why

IDecoder::Start() 当前同时承担"注入回调"和"启动解码线程"两个职责。每次新增回调（如未来的 OnError）都需要修改 Start() 签名，导致所有实现类和调用方联动修改，违反开闭原则。此外，项目中 AudioRenderer 已采用 SetXxxCallback 模式，当前 IDecoder 与其不一致。

## What Changes

- **BREAKING**: `IDecoder::Start(PacketQueue*, MediaFrameCallback, EofOutputCallback)` 签名变更为 `IDecoder::Start(PacketQueue*)`
- IDecoder 新增 `SetFrameCallback(MediaFrameCallback)` 和 `SetEofCallback(EofOutputCallback)` 两个纯虚方法
- AVFrameDecoder 实现新增的 setter 方法，内部存储回调
- StreamContext::OpenDecoder() 阶段完成回调注入，Start() 仅负责启动

## Capabilities

### New Capabilities

（无）

### Modified Capabilities

- `decoder-interface`: IDecoder 接口签名变更 — Start() 参数缩减，新增两个 setter 纯虚方法
- `stream-context`: Start() 不再传递回调；回调注入移至 OpenDecoder() 阶段

## Impact

- `src/core/src/i_decoder.h`: 接口定义变更
- `src/core/src/decoder.h/cc`: AVFrameDecoder 实现适配
- `src/core/src/stream_context.h/cc`: 调用时序调整
- `openspec/specs/decoder-interface/spec.md`: 需求描述更新
- `openspec/specs/stream-context/spec.md`: Start/OpenDecoder 需求描述更新
