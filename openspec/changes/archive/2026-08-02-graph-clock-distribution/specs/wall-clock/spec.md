## MODIFIED Requirements

### Requirement: Clock uses SeqLock for thread safety
Clock SHALL 使用 SeqLock（序列锁）实现线程安全。写端（Set/SetPaused/SetSpeed/Reset）递增序列号为奇数表示写入中，写完后递增为偶数。读端（Get）循环读取直到前后序列号一致且为偶数。

写端之间 SHALL 由互斥量串行化：时钟同时被节点线程（帧 PTS 更新）与控制线程（暂停/重置）写入，序列号本身只保证读端不撕裂，不保证多写者互不交错。读端 SHALL 保持完全无锁。

#### Scenario: Concurrent read during write
- **WHEN** 写线程正在执行 Set()（seq 为奇数）同时读线程调用 Get()
- **THEN** 读线程重试直到写完成，返回一致的值，无数据竞争

#### Scenario: Multiple readers concurrent
- **WHEN** 多个线程同时调用 Get()
- **THEN** 所有读线程均能无阻塞获取一致值

#### Scenario: Concurrent writers serialize
- **WHEN** 音频线程调用 Set() 的同时控制线程调用 SetPaused()
- **THEN** 两次写入串行发生，序列号不会交错递增，读端始终看到完整快照

## ADDED Requirements

### Requirement: Clock implements the IClock interface
系统 SHALL 在 `clock.h` 中定义抽象接口 `IClock`（`Set` / `Get` / `SetPaused` / `SetSpeed` / `Reset`），并由 `Clock` 实现。

`IClock` SHALL 位于命名空间 `mvp` 且所在头文件 SHALL NOT 依赖 graph 层任何类型——它是被 graph 层引用的叶子抽象，反向依赖会形成循环。

#### Scenario: Graph holds clocks through the interface
- **WHEN** MediaGraph 收集并保存节点提供的时钟
- **THEN** 以 `IClock` 引用持有，不依赖具体实现类型

#### Scenario: Clock header stays dependency-free
- **WHEN** 编译 `clock.h`
- **THEN** 仅需标准库头文件，不引入 MediaGraph 或任何节点声明
