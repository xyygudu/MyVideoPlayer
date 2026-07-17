## ADDED Requirements

### Requirement: MediaFrame 提供平面数据访问方法
`MediaFrame` SHALL 新增以下 const 和非 const 方法，封装 `AVFrame` 内部平面布局的访问，使调用方无需通过 `RawFrame()` 获取裸指针即可读写像素数据：
- `int width() const`
- `int height() const`
- `int format() const`（返回 `AVPixelFormat` 裸 int）
- `const uint8_t* PlaneData(int plane) const`
- `uint8_t* PlaneData(int plane)`
- `int PlaneLinesize(int plane) const`

非 const `PlaneData(int)` 仅应在调用方已通过 `MakeWritable()` 获取可写副本后使用。

#### Scenario: 通过 PlaneData 访问 Y 平面
- **WHEN** 调用 `frame.PlaneData(0)` 和 `frame.PlaneLinesize(0)` 获取 Y 平面的数据指针和行跨度
- **THEN** 返回值与 `av_frame` 内部 `data[0]`/`linesize[0]` 一致，无需调用 `RawFrame()`

### Requirement: MediaFrame 提供 MakeWritable 可写性保障
`MediaFrame` SHALL 提供 `MediaFrame MakeWritable() const` 方法。若内部 AVFrame 引用计数为 1（独占），返回的 `MediaFrame` 与当前对象共享同一内存（通过 move）；否则深拷贝一份新的并返回。调用方 SHALL 通过返回值操作可写帧，不通过原始对象。

声明 SHALL 带 `[[nodiscard]]`，防止调用方忽略返回值导致写操作落到只读帧。

#### Scenario: 引用计数为 1 时原位可写
- **WHEN** 某 MediaFrame 仅被一处引用，调用 `auto w = mf.MakeWritable()`
- **THEN** `w.PlaneData(0)` 与 `mf.PlaneData(0)` 指向相同地址，不触发内存拷贝

#### Scenario: 共享帧触发深拷贝
- **WHEN** 某 MediaFrame 被多处引用（通过 `av_frame_ref` 共享），调用 `MakeWritable()`
- **THEN** 返回的帧拥有独立的内存副本，修改 `w.PlaneData(0)` 不影响原始帧

### Requirement: MediaFrame 提供 CreateSameFormat 工厂方法
`MediaFrame` SHALL 提供 `static MediaFrame CreateSameFormat(const MediaFrame& ref, double pts)`。返回的帧具有与 `ref` 相同的 `width`/`height`/`format`，但 `data[]` 各平面为零初始化（不拷贝像素数据）。

#### Scenario: 创建同尺寸空帧
- **WHEN** 调用 `MediaFrame::CreateSameFormat(src_frame, 0.5)`
- **THEN** 返回帧的尺寸和格式与 src_frame 相同，PTS 为 0.5，平面数据为全零
