#ifndef MVP_SYNC_CONSTANTS_H_
#define MVP_SYNC_CONSTANTS_H_

#include <cstdint>

namespace mvp::sync {

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

// Default maximum frame count for the video FrameQueue.
// Aligned with FFplay VIDEO_PICTURE_QUEUE_SIZE.
inline constexpr int kDefaultVideoQueueSize = 3;

// Default maximum frame count for the audio FrameQueue.
// Aligned with FFplay SAMPLE_QUEUE_SIZE.
inline constexpr int kDefaultAudioQueueSize = 9;

// Default maximum byte budget for a PacketQueue.
// Aligned with FFplay MAX_QUEUE_SIZE (15 MB).
inline constexpr int64_t kDefaultMaxQueueBytes = 15 * 1024 * 1024;

// Seek target sentinel: indicates no seek is in progress.
inline constexpr double kNoSeekTarget = -1.0;

}  // namespace mvp::sync

#endif  // MVP_SYNC_CONSTANTS_H_
