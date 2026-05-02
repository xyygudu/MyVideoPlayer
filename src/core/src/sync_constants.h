#ifndef MVP_SYNC_CONSTANTS_H_
#define MVP_SYNC_CONSTANTS_H_

#include <cstdint>

namespace mvp::sync {

// A/V sync: if video is ahead of audio by more than this, sleep to wait.
// ~40ms ≈ 1 frame at 25fps.
inline constexpr double kSyncThreshold = 0.04;

// A/V sync: if video is behind audio by more than this, drop the frame.
// 100ms — perceptible lip-sync boundary.
inline constexpr double kDropThreshold = 0.1;

// Maximum sleep duration per iteration to remain responsive to seek/stop.
inline constexpr double kMaxSleepSeconds = 0.1;

// VideoMaster mode: minimum valid frame interval (below this, use fallback).
inline constexpr double kFrameDelayMin = 0.001;

// VideoMaster mode: maximum valid frame interval (above this, use fallback).
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
