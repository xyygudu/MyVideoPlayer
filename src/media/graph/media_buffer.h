#ifndef MVP_GRAPH_MEDIA_BUFFER_H_
#define MVP_GRAPH_MEDIA_BUFFER_H_

#include <cstdint>
#include <variant>

#include "ffmpeg_utils.h"
#include "media_frame.h"

namespace mvp::graph {

/// Bit flags carried by each buffer through the graph.
enum class BufferFlags : uint32_t {
    kNone = 0,
    kEos = 1 << 0,           // End-of-stream marker
    kDiscontinuity = 1 << 1, // PTS discontinuity (after seek)
    kKeyFrame = 1 << 2,      // Contains a keyframe
    kCorrupt = 1 << 3,       // Data is corrupted
};

inline BufferFlags operator|(BufferFlags a, BufferFlags b) {
    return static_cast<BufferFlags>(static_cast<uint32_t>(a) |
                                    static_cast<uint32_t>(b));
}

inline BufferFlags operator&(BufferFlags a, BufferFlags b) {
    return static_cast<BufferFlags>(static_cast<uint32_t>(a) &
                                    static_cast<uint32_t>(b));
}

inline bool HasFlag(BufferFlags flags, BufferFlags flag) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

/// Rational number for time base representation.
struct Rational {
    int num{0};
    int den{1};
};

/// Timestamp information carried by each MediaBuffer.
struct Timestamp {
    double pts{0.0};     // Presentation timestamp in seconds
    // Native time base of the payload. Only meaningful for packets, where the
    // muxer rescales the AVPacket's own pts/dts with it; frame PTS is already
    // in seconds.
    Rational time_base;
};

/// Unified data carrier flowing through graph Links.
///
/// Payload is a type-safe variant: either compressed packet (AVPacketPtr)
/// or decoded frame (MediaFrame). Move-only semantics.
///
/// Owns all transport metadata. Media type is deliberately absent: it is a
/// property of the link (fixed at negotiation, readable via
/// InputPort::Format()), not of each buffer.
class MediaBuffer {
  public:
    using Payload = std::variant<std::monostate, AVPacketPtr, MediaFrame>;

    MediaBuffer() = default;

    /// Construct with packet payload.
    explicit MediaBuffer(AVPacketPtr pkt, Timestamp ts = {},
                         BufferFlags flags = BufferFlags::kNone);

    /// Construct with frame payload.
    explicit MediaBuffer(MediaFrame frame, Timestamp ts = {},
                         BufferFlags flags = BufferFlags::kNone);

    /// Construct an EOS-only buffer (no payload). The seek epoch is required:
    /// an unstamped EOS is dropped as stale and playback never reports end.
    static MediaBuffer MakeEos(int serial);

    ~MediaBuffer();

    MediaBuffer(MediaBuffer&& other) noexcept;
    MediaBuffer& operator=(MediaBuffer&& other) noexcept;

    MediaBuffer(const MediaBuffer&) = delete;
    MediaBuffer& operator=(const MediaBuffer&) = delete;

    // --- Type queries ---
    bool IsPacket() const;
    bool IsFrame() const;
    bool IsValid() const;  // Has a non-empty payload

    // --- Type-safe payload access ---
    AVPacketPtr& AsPacket();
    const AVPacketPtr& AsPacket() const;
    MediaFrame& AsFrame();
    const MediaFrame& AsFrame() const;

    // --- Metadata ---
    Timestamp timestamp() const { return timestamp_; }
    BufferFlags flags() const { return flags_; }
    int serial() const { return serial_; }

    void set_timestamp(Timestamp ts) { timestamp_ = ts; }
    void set_flags(BufferFlags flags) { flags_ = flags; }
    void set_serial(int serial) { serial_ = serial; }

  private:
    Payload payload_;
    Timestamp timestamp_;
    BufferFlags flags_{BufferFlags::kNone};
    int serial_{0};
};

}  // namespace mvp::graph

#endif  // MVP_GRAPH_MEDIA_BUFFER_H_
