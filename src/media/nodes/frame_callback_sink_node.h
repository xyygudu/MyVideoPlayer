#ifndef MVP_NODES_FRAME_CALLBACK_SINK_NODE_H_
#define MVP_NODES_FRAME_CALLBACK_SINK_NODE_H_

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "graph/node.h"
#include "graph/port.h"
#include "mvp/captured_frame.h"

struct SwsContext;

namespace mvp::graph {

/// Hands every video frame to a callback as RGBA, scaled down to at most
/// `max_width` pixels wide: a preview sink (e.g. of a camera) for UIs that
/// draw the picture themselves. Active; the callback runs on its thread.
class FrameCallbackSinkNode : public INode {
  public:
    using Callback = std::function<void(const CapturedFrame& frame)>;

    FrameCallbackSinkNode(int max_width, Callback callback);
    ~FrameCallbackSinkNode() override;

    void DeclareCaps() override;
    bool Negotiate() override;
    bool Prepare() override;
    bool Start() override;
    void Stop() override;
    void Flush() override {}

    std::vector<InputPort*> Inputs() override { return {input_port_.get()}; }
    std::vector<OutputPort*> Outputs() override { return {}; }
    NodeType Type() const override { return NodeType::kSink; }
    ThreadingMode Threading() const override { return ThreadingMode::kActive; }
    NodeState State() const override { return state_; }
    std::string Name() const override { return "FrameCallbackSinkNode"; }

  private:
    void DeliverLoop();
    bool Convert(const MediaBuffer& buf, CapturedFrame* out);

    const int max_width_;
    const Callback callback_;
    std::unique_ptr<InputPort> input_port_;
    NodeState state_{NodeState::kIdle};
    std::thread thread_;
    std::atomic<bool> running_{false};
    SwsContext* sws_{nullptr};  // Sink thread only
};

}  // namespace mvp::graph

#endif  // MVP_NODES_FRAME_CALLBACK_SINK_NODE_H_
