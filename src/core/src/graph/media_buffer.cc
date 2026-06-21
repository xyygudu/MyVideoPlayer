#include "graph/media_buffer.h"

namespace mvp::graph {

MediaBuffer::MediaBuffer(AVPacketPtr pkt, MediaType type, Timestamp ts,
                         BufferFlags flags)
    : payload_(std::move(pkt)),
      media_type_(type),
      timestamp_(ts),
      flags_(flags) {}

MediaBuffer::MediaBuffer(MediaFrame frame, Timestamp ts, BufferFlags flags)
    : payload_(std::move(frame)),
      media_type_(frame.type()),
      timestamp_(ts),
      flags_(flags) {}

MediaBuffer MediaBuffer::MakeEos(MediaType type) {
    MediaBuffer buf;
    buf.media_type_ = type;
    buf.flags_ = BufferFlags::kEos;
    return buf;
}

MediaBuffer::~MediaBuffer() = default;

MediaBuffer::MediaBuffer(MediaBuffer&& other) noexcept
    : payload_(std::move(other.payload_)),
      media_type_(other.media_type_),
      timestamp_(other.timestamp_),
      flags_(other.flags_),
      serial_(other.serial_) {
    other.flags_ = BufferFlags::kNone;
    other.serial_ = 0;
}

MediaBuffer& MediaBuffer::operator=(MediaBuffer&& other) noexcept {
    if (this != &other) {
        payload_ = std::move(other.payload_);
        media_type_ = other.media_type_;
        timestamp_ = other.timestamp_;
        flags_ = other.flags_;
        serial_ = other.serial_;
        other.flags_ = BufferFlags::kNone;
        other.serial_ = 0;
    }
    return *this;
}

bool MediaBuffer::IsPacket() const {
    return std::holds_alternative<AVPacketPtr>(payload_);
}

bool MediaBuffer::IsFrame() const {
    return std::holds_alternative<MediaFrame>(payload_);
}

bool MediaBuffer::IsValid() const {
    if (IsPacket()) {
        return std::get<AVPacketPtr>(payload_).get() != nullptr;
    }
    if (IsFrame()) {
        return std::get<MediaFrame>(payload_).IsValid();
    }
    // EOS-only buffers are valid if they have the EOS flag.
    return HasFlag(flags_, BufferFlags::kEos);
}

AVPacketPtr& MediaBuffer::AsPacket() {
    return std::get<AVPacketPtr>(payload_);
}

const AVPacketPtr& MediaBuffer::AsPacket() const {
    return std::get<AVPacketPtr>(payload_);
}

MediaFrame& MediaBuffer::AsFrame() {
    return std::get<MediaFrame>(payload_);
}

const MediaFrame& MediaBuffer::AsFrame() const {
    return std::get<MediaFrame>(payload_);
}

}  // namespace mvp::graph
