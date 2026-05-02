#ifndef MVP_PLAYER_STATE_H_
#define MVP_PLAYER_STATE_H_

#include "mvp/export.h"

namespace mvp {

// Primary playback state machine.
// Transitions: Idle→Ready (Open), Ready→Playing (Play), Playing→Paused (Pause),
//              Paused→Playing (Play), Playing/Paused→Finished (EOF),
//              Any→Idle (Close), Finished→Paused (Seek).
enum class MVP_CORE_EXPORT PlayerState {
    Idle,      // No file open, or closed
    Ready,     // File opened, not yet playing
    Playing,   // Active playback
    Paused,    // Paused, threads alive but waiting
    Finished,  // Reached end of file
};

// Determines which clock drives the synchronization.
enum class SyncMode {
    AudioMaster,  // Audio clock is the reference (default when audio exists)
    VideoMaster,  // Video clock is self-driven (pure video files)
};

}  // namespace mvp

#endif  // MVP_PLAYER_STATE_H_
