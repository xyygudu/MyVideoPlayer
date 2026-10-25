#ifndef MVP_CAPTURE_WIN_MEDIA_FOUNDATION_H_
#define MVP_CAPTURE_WIN_MEDIA_FOUNDATION_H_

// Media Foundation helpers shared by the camera source and enumeration.

#include "capture/win/win_util.h"  // First: configures <windows.h>

#include <mfapi.h>

#include <vector>

#include "mvp/capture_devices.h"

namespace mvp::capture::win {

/// Media Foundation started for the scope (MFStartup is reference counted).
class ScopedMediaFoundation {
  public:
    ScopedMediaFoundation() : hr_(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET)) {}
    ~ScopedMediaFoundation() {
        if (SUCCEEDED(hr_)) MFShutdown();
    }
    ScopedMediaFoundation(const ScopedMediaFoundation&) = delete;
    ScopedMediaFoundation& operator=(const ScopedMediaFoundation&) = delete;

    bool Ok() const { return SUCCEEDED(hr_); }

  private:
    HRESULT hr_;
};

/// Video capture devices; ids are device symbolic links. Needs COM
/// initialized on the calling thread.
std::vector<CameraInfo> EnumerateCameras();

}  // namespace mvp::capture::win

#endif  // MVP_CAPTURE_WIN_MEDIA_FOUNDATION_H_
