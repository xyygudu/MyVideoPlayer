#ifndef MVP_DEMUXER_H_
#define MVP_DEMUXER_H_

#include <atomic>
#include <string>
#include <thread>

struct AVFormatContext;

namespace mvp {

class PacketQueue;

class Demuxer {
  public:
    Demuxer();
    ~Demuxer();

    Demuxer(const Demuxer&) = delete;
    Demuxer& operator=(const Demuxer&) = delete;

    bool Open(const std::string& filepath);
    void Close();

    void Start(PacketQueue* audio_queue, PacketQueue* video_queue);
    void Stop();

    // Seek to position in seconds. Thread-safe.
    void RequestSeek(double position_seconds);

    int AudioStreamIndex() const { return audio_stream_index_; }
    int VideoStreamIndex() const { return video_stream_index_; }
    AVFormatContext* FormatContext() const { return format_ctx_; }
    double Duration() const;

  private:
    void DemuxLoop();

    AVFormatContext* format_ctx_;
    int audio_stream_index_;
    int video_stream_index_;

    PacketQueue* audio_queue_;
    PacketQueue* video_queue_;

    std::thread demux_thread_;
    std::atomic<bool> running_;
    std::atomic<bool> seek_requested_;
    std::atomic<double> seek_position_;
};

}  // namespace mvp

#endif  // MVP_DEMUXER_H_
