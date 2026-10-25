## 1. 图与上报

- [x] 1.1 `graph/message_bus.h`：`GraphMessage{type, source, serial, detail, pts}`、`MessageBus{Post, Wait, Shutdown}`；删除 `GraphEvent`/`SetEventCallback`/`GraphState`
- [x] 1.2 `Link::Abort` 在锁内置位，消除丢失唤醒
- [x] 1.3 VideoSinkNode：每世代首帧投递 `kFirstFrame`，设备丢失投递 `kDeviceLost`；发布屏幕帧（`DisplayedPosition`/`DisplayedFrame`/`FramesPresented`）

## 2. MediaPlayer

- [x] 2.1 拆分为 `media_player_impl.h`、`media_player.cc`（生命周期/控制/查询）、`media_player_pipeline.cc`（建图/重建/事件线程）
- [x] 2.2 事件线程：按世代汇总 EOS、错误冻结输出、完成回调锁外调用
- [x] 2.3 `PlayerOptions`/`SetOptions`/`RebuildLocked`（位置、状态、特效保持）
- [x] 2.4 暂停刷新：`RefreshLocked`/`SettleEpochLocked`，`kFirstFrame` 驱动合并
- [x] 2.5 `ResetEffectParams`（`IEffectNode::ResetParams` → `EffectManager::ResetParams`）
- [x] 2.6 `GetPipelineInfo`、`CaptureFrame`（`frame_capture.{h,cc}`，按帧矩阵/范围转换）
- [x] 2.7 设备丢失：`VideoRenderer::RecoverDevice` + 事件线程重建（10 秒内最多 3 次）

## 3. 同步与时间戳

- [x] 3.1 `ComputeSlavedDelay` 改用视频时钟偏差，`kMaxFrameDuration` 防跳变；`WaitForSlot` 分片等待不封顶
- [x] 3.2 DemuxNode 按 start_time 平移时间轴；`DemuxOptions::correct_ts_discontinuities` 折叠跳变（转码开启）

## 4. 转码

- [x] 4.1 `TranscodeResult` 三态，取消在调用线程同步上报
- [x] 4.2 编码参数协商、缩放、共享 `AudioResampler`
- [x] 4.3 EncoderNode 丢弃 pts 不递增的视频帧

## 5. 特效几何

- [x] 5.1 `ComputeAffineMapping` 与 `affine_remap_ps.hlsl` 中心改为 (n−1)/2
- [x] 5.2 `TryPermutePlane` 只处理精确四分之一圈，支持分量偏移；NV12 双线性使用色度映射

## 6. 应用层

- [x] 6.1 `SettingsPage`：管线开关（QSettings 持久化）、诊断（可见时 500ms 刷新）、日志级别/控制台/日志目录、截图
- [x] 6.2 导航栏底部"设置"项，`Page::kSettings`，`MainWindow` 路由
- [x] 6.3 EffectPanel 每组"重置参数"按钮（排队重建控件）
- [x] 6.4 `logging::SetLevel/GetLevel/IsConsoleLoggingEnabled/FileLogPath`

## 7. 构建与测试

- [x] 7.1 `mvp_core_objects` OBJECT 库；`export.h` 生成到 `<build>/generated/include`；GLOB 加 `CONFIGURE_DEPENDS`
- [x] 7.2 `tests/`：GoogleTest v1.15.2（SHA256 固定）、`generate_test_media.cmake` 构建期生成素材
- [x] 7.3 单元测试：Link、MessageBus、Clock、AudioResampler
- [x] 7.4 集成测试：播放/重播、软解 seek 到尾后重播、三条特效路径的暂停刷新与镜像精确性、跨路径变换一致、重置、解码切换、低帧率同步、TS 时间轴、截图色彩、多编解码器播放与 seek
- [x] 7.5 转码测试：完整性、Opus 重采样、TS 跳变折叠、中途格式变化、取消、缺失输入
- [x] 7.6 全部测试通过（39 项，真实素材目录 19 个文件通过）
