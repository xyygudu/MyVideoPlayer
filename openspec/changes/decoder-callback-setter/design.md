## Context

IDecoder 是解码器的抽象接口，当前唯一实现为 AVFrameDecoder。StreamContext 作为管线封装层，在 Start() 中创建回调 lambda 并通过 IDecoder::Start() 一次性传入。AudioRenderer 已采用 SetEofCallback() setter 模式注入回调。

## Goals / Non-Goals

**Goals:**
- IDecoder::Start() 职责收窄为"启动解码线程"，不再承担回调注入
- 新增回调时不需要修改 Start() 签名（开闭原则）
- 与 AudioRenderer 的 callback setter 模式保持对称

**Non-Goals:**
- 不改变回调的语义或签名（MediaFrameCallback、EofOutputCallback 保持不变）
- 不引入 Observer/Listener 接口模式
- 不调整 PlayerImpl 的调用方式（PlayerImpl 只接触 StreamContext）

## Decisions

### Decision 1: Setter 作为 IDecoder 纯虚方法

在 IDecoder 接口上新增 `SetFrameCallback` 和 `SetEofCallback` 两个纯虚方法。

**选择理由**：回调是 IDecoder 契约的一部分（所有实现都必须支持输出帧和 EOF 通知），放在接口上是自然的。

**备选方案**：仅在 AVFrameDecoder 具体类上加 setter → 违反依赖倒置，StreamContext 需感知具体类型。

### Decision 2: 回调注入时机放在 OpenDecoder 阶段

StreamContext::OpenDecoder() 在调用 decoder_->Open() 之后立即调用 SetFrameCallback / SetEofCallback。Start() 仅调用 decoder_->Start(&packet_queue_)。

**选择理由**：
- 配置与启动完全分离，职责清晰
- 与 AudioRenderer 的使用模式一致（Open → SetEofCallback → Start）
- 避免 Start() 每次调用都重复设置相同的回调

**备选方案**：在 StreamContext::Start() 中 Set 后再 Start → 可行但配置和启动仍然耦合在一个方法调用中。

### Decision 3: AVFrameDecoder::Start() 增加前置断言

Start() 入口处 assert 回调已设置，将"忘记 Set"的错误从运行时静默失败提前到开发期崩溃。

```cpp
assert(on_frame_ && "Must call SetFrameCallback before Start");
```

## Risks / Trade-offs

- **时序耦合风险**：调用方可能忘记在 Start 前 Set 回调 → 通过 assert 缓解；且 StreamContext 是唯一调用方，风险可控
- **接口膨胀**：IDecoder 从 4 个纯虚方法增加到 6 个 → 增量合理，且新方法是对称的 setter pair
