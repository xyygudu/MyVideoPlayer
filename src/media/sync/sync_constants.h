#ifndef MVP_SYNC_SYNC_CONSTANTS_H_
#define MVP_SYNC_SYNC_CONSTANTS_H_

namespace mvp {

// A/V sync threshold lower bound (aligned with FFplay AV_SYNC_THRESHOLD_MIN).
// ~40ms ≈ 1 frame at 25fps. Below this, diff is imperceptible.
inline constexpr double kSyncThresholdMin = 0.04;

// A/V sync threshold upper bound & frame-dup boundary (aligned with FFplay
// AV_SYNC_THRESHOLD_MAX / AV_SYNC_FRAMEDUP_THRESHOLD).
// 100ms — perceptible lip-sync boundary (ITU-R BT.1359).
// Used as: (1) clamp ceiling for adaptive sync_threshold,
//          (2) frame-interval boundary separating 2*delay vs delay+diff.
inline constexpr double kSyncThresholdMax = 0.1;

// Maximum sleep duration per iteration to remain responsive to seek/stop.
inline constexpr double kMaxSleepSeconds = 0.1;

// Minimum valid frame interval (below this, use fallback). All sync modes.
inline constexpr double kFrameDelayMin = 0.001;

// Maximum valid frame interval (above this, use fallback). All sync modes.
inline constexpr double kFrameDelayMax = 1.0;

// Clock differences beyond this are timestamp discontinuities, not drift: the
// video is not slowed down or sped up to chase them (FFplay max_frame_duration
// for inputs with timestamp discontinuities).
inline constexpr double kMaxFrameDuration = 10.0;

// Queue depths and byte budgets live next to their users: graph/link.h
// (packet links) and the facades that size their frame links.

}  // namespace mvp

#endif  // MVP_SYNC_SYNC_CONSTANTS_H_
