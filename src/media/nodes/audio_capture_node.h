#ifndef MVP_NODES_AUDIO_CAPTURE_NODE_H_
#define MVP_NODES_AUDIO_CAPTURE_NODE_H_

#include <memory>
#include <string>

#include "mvp/capture_devices.h"
#include "nodes/live_source_node.h"

namespace mvp::capture {
class AudioSource;
}  // namespace mvp::capture

namespace mvp::graph {

/// Captures an audio endpoint: an output device through loopback (what the
/// speakers/headphones play) or an input device (microphone). Frames keep
/// the device's own format; the mixer converts.
///
/// Each packet is stamped with the running time at which its first sample
/// was captured (the device's QPC timestamp), not when it was read.
///
/// An unplugged device is reopened every second: the default device id
/// ("") then follows Windows to the new default. Meanwhile the stream just
/// has a gap, which the mixer fills with silence.
class AudioCaptureNode : public LiveSourceNode {
  public:
    AudioCaptureNode(AudioDeviceKind kind, std::string device_id);
    ~AudioCaptureNode() override;

    std::string Name() const override { return name_; }

  private:
    bool OpenDevice(MediaFormat* format) override;
    void CaptureStep() override;
    void CloseDevice() override;
    /// Hands everything the device buffered downstream; false once lost.
    bool ReadPackets();

    const AudioDeviceKind kind_;
    const std::string device_id_;
    const std::string name_;

    // Device thread only.
    std::unique_ptr<capture::AudioSource> source_;
    bool device_lost_{false};
};

}  // namespace mvp::graph

#endif  // MVP_NODES_AUDIO_CAPTURE_NODE_H_
