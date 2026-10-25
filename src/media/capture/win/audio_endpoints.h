#ifndef MVP_CAPTURE_WIN_AUDIO_ENDPOINTS_H_
#define MVP_CAPTURE_WIN_AUDIO_ENDPOINTS_H_

// MMDevice helpers shared by the WASAPI source and device enumeration.

#include "capture/win/win_util.h"  // First: configures <windows.h>

#include <mmdeviceapi.h>

#include <string>
#include <vector>

#include "mvp/capture_devices.h"

namespace mvp::capture::win {

/// User-visible name ("Speakers (Realtek Audio)"); empty when unavailable.
std::string EndpointName(IMMDevice* device);

/// Stable endpoint id accepted by IMMDeviceEnumerator::GetDevice.
std::string EndpointId(IMMDevice* device);

/// Active endpoints of one direction; the console default is flagged.
/// Needs COM initialized on the calling thread.
std::vector<AudioDeviceInfo> EnumerateAudioEndpoints(AudioDeviceKind kind);

}  // namespace mvp::capture::win

#endif  // MVP_CAPTURE_WIN_AUDIO_ENDPOINTS_H_
