## ADDED Requirements

### Requirement: Clock stores PTS with wall-time reference
Clock SHALL 存储一个 PTS 值和设置该值时的系统时钟时间戳（wall-time）。Get() SHALL 返回 `pts + (now - last_updated) * speed`，即基于系统时间的线性外推值。

#### Scenario: Clock advances between Set calls
- **WHEN** 调用 `Set(10.0)` 后经过 0.5 秒再调用 `Get()`
- **THEN** 返回值约为 10.5（在 speed=1.0 时）

#### Scenario: Clock Get immediately after Set
- **WHEN** 调用 `Set(5.0)` 后立即调用 `Get()`
- **THEN** 返回值为 5.0（elapsed ≈ 0）

### Requirement: Clock supports pause and resume
Clock SHALL 提供 `SetPaused(bool)` 接口。暂停时 Get() SHALL 返回冻结值（暂停瞬间的外推值）。恢复时 SHALL 重设 last_updated 为当前系统时间，使时钟从冻结值继续推进。

#### Scenario: Get returns frozen value when paused
- **WHEN** Set(10.0) 后暂停，再经过 2 秒调用 Get()
- **THEN** 返回值仍为 10.0（不随时间推进）

#### Scenario: Resume continues from frozen value
- **WHEN** 暂停时 Get() 为 10.0，恢复后经过 1 秒调用 Get()
- **THEN** 返回值约为 11.0

### Requirement: Clock supports playback speed
Clock SHALL 提供 `SetSpeed(double)` 接口。Get() 的外推公式中 elapsed 乘以 speed 因子。默认 speed SHALL 为 1.0。

#### Scenario: Double speed playback
- **WHEN** SetSpeed(2.0) 后 Set(0.0)，经过 1 秒调用 Get()
- **THEN** 返回值约为 2.0

#### Scenario: Default speed is 1.0
- **WHEN** 未调用 SetSpeed，Set(0.0) 后经过 1 秒调用 Get()
- **THEN** 返回值约为 1.0

### Requirement: Clock Reset clears state
Clock SHALL 提供 `Reset(double pts = 0.0)` 接口，等同于 Set(pts) 但语义表示完全重置。

#### Scenario: Reset to zero
- **WHEN** 调用 Reset()
- **THEN** Get() 返回约 0.0（加上极小 elapsed）

#### Scenario: Reset to specific position
- **WHEN** 调用 Reset(30.0)
- **THEN** Get() 返回约 30.0

### Requirement: Clock uses SeqLock for thread safety
Clock SHALL 使用 SeqLock（序列锁）实现线程安全。写端（Set/SetPaused/SetSpeed/Reset）递增序列号为奇数表示写入中，写完后递增为偶数。读端（Get）循环读取直到前后序列号一致且为偶数。

#### Scenario: Concurrent read during write
- **WHEN** 写线程正在执行 Set()（seq 为奇数）同时读线程调用 Get()
- **THEN** 读线程重试直到写完成，返回一致的值，无数据竞争

#### Scenario: Multiple readers concurrent
- **WHEN** 多个线程同时调用 Get()
- **THEN** 所有读线程均能无阻塞获取一致值

### Requirement: Clock uses steady_clock as time source
Clock SHALL 使用 `std::chrono::steady_clock` 作为系统时间源（单调递增，不受系统时间调整影响）。

#### Scenario: System time adjustment does not affect clock
- **WHEN** 系统时间被手动调整（NTP 跳变等）
- **THEN** Clock 的 Get() 不受影响，仍基于 steady_clock 单调推进
