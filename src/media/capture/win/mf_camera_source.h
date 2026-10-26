#ifndef MVP_CAPTURE_WIN_MF_CAMERA_SOURCE_H_
#define MVP_CAPTURE_WIN_MF_CAMERA_SOURCE_H_

#include "capture/win/win_util.h"  // First: configures <windows.h>

#include <mfidl.h>
#include <wrl/client.h>

#include <functional>
#include <memory>
#include <string>

#include "capture/camera_source.h"

namespace mvp::capture::win {

/// Creates the media source to read, on the capture thread with COM and Media Foundation started,
/// and names it for logs. Null when there is nothing to open.
using MediaSourceFactory = std::function<Microsoft::WRL::ComPtr<IMFMediaSource>(std::string* name)>;

/// The Media Foundation camera source over any media source: CreateCameraSource() passes one that
/// opens a capture device, tests pass their own.
std::unique_ptr<CameraSource> CreateMfCameraSource(MediaSourceFactory factory);

}  // namespace mvp::capture::win

#endif  // MVP_CAPTURE_WIN_MF_CAMERA_SOURCE_H_
