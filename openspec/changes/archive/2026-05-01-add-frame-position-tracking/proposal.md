## Why

当前 UI 进度条通过 `audio_clock_` 获取播放位置，只能反映"音频播到了哪里"，无法精确到当前屏幕显示的是哪一帧。对于后续逐帧操作（上一帧/下一帧）和帧号显示需求，需要一个帧级精确的视频位置数据源。

## What Changes

- Player 核心层新增 `video_pts_` (atomic) 记录最后渲染帧的 PTS
- VideoRenderLoop 每次渲染帧后更新 `video_pts_`
- 新增 `Player::CurrentVideoPosition()` public API 暴露帧级精确位置
- UI 进度条改为读取视频位置，并显示帧号信息（如 `Frame 74 / 2700`）
- 新增 `Player::VideoFps()` API 供 UI 计算帧号

## Capabilities

### New Capabilities
- `frame-position`: 帧级精确的视频播放位置追踪与显示

### Modified Capabilities
- `playback-control`: 新增 `CurrentVideoPosition()` 和 `VideoFps()` API 到 Player 公共接口

## Impact

- `src/core/src/player.cc` — PlayerImpl 新增 atomic 成员和方法
- `src/core/include/mvp/player.h` — Player 公共接口新增方法
- `src/app/src/main_window.cc` — UI 层进度更新逻辑和显示格式调整
- 无破坏性变更，audio_clock 保留用于音视频同步
