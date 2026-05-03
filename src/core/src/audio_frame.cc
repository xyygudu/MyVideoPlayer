#include "mvp/audio_frame.h"

#include "frame_impl.h"

namespace mvp {

AudioFrame::AudioFrame() = default;
AudioFrame::~AudioFrame() = default;

AudioFrame::AudioFrame(AudioFrame&& other) noexcept = default;
AudioFrame& AudioFrame::operator=(AudioFrame&& other) noexcept = default;

const uint8_t* AudioFrame::data() const {
    if (!impl_ || !impl_->frame) return nullptr;
    return impl_->frame->data[0];
}

int AudioFrame::nb_samples() const {
    if (!impl_ || !impl_->frame) return 0;
    return impl_->frame->nb_samples;
}

int AudioFrame::channels() const {
    if (!impl_ || !impl_->frame) return 0;
    return impl_->frame->ch_layout.nb_channels;
}

int AudioFrame::sample_rate() const {
    if (!impl_ || !impl_->frame) return 0;
    return impl_->frame->sample_rate;
}

SampleFormat AudioFrame::format() const {
    if (!impl_) return SampleFormat::kUnknown;
    return impl_->format;
}

double AudioFrame::pts() const {
    if (!impl_) return 0.0;
    return impl_->pts;
}

bool AudioFrame::IsValid() const {
    return impl_ && impl_->frame && impl_->frame->data[0];
}

}  // namespace mvp
