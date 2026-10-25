// Concurrency primitives of the graph: wakeups that used to be lost, stale
// messages and clock updates across seek epochs.
#include <chrono>
#include <future>
#include <thread>

#include <gtest/gtest.h>

#include "graph/link.h"
#include "graph/message_bus.h"
#include "sync/clock.h"

namespace mvp {
namespace {

using namespace std::chrono_literals;

// Abort must wake a consumer that is already waiting, whatever the timing
// (Abort used to set its flag without the mutex, so a wakeup could be lost).
TEST(LinkTest, AbortWakesBlockedConsumer) {
    for (int i = 0; i < 200; ++i) {
        graph::Link link(graph::LinkCapacity::ForFrames(1));
        auto popped = std::async(std::launch::async, [&link] { return link.Pop(); });
        std::this_thread::sleep_for(std::chrono::microseconds((i % 5) * 100));
        link.Abort();
        ASSERT_EQ(popped.wait_for(2s), std::future_status::ready) << "iteration " << i;
        EXPECT_FALSE(popped.get().has_value());
    }
}

TEST(LinkTest, AbortWakesBlockedProducer) {
    graph::Link link(graph::LinkCapacity::ForFrames(1));
    ASSERT_TRUE(link.Push(graph::MediaBuffer::MakeEos(0)));
    auto pushed = std::async(std::launch::async,
                             [&link] { return link.Push(graph::MediaBuffer::MakeEos(0)); });
    EXPECT_EQ(pushed.wait_for(50ms), std::future_status::timeout) << "a full link must block";
    link.Abort();
    ASSERT_EQ(pushed.wait_for(2s), std::future_status::ready);
    EXPECT_FALSE(pushed.get());
}

TEST(LinkTest, AbortedLinkStillDrainsQueuedBuffers) {
    graph::Link link(graph::LinkCapacity::ForFrames(2));
    ASSERT_TRUE(link.Push(graph::MediaBuffer::MakeEos(3)));
    link.Abort();
    std::optional<graph::MediaBuffer> buf = link.Pop();
    ASSERT_TRUE(buf.has_value());
    EXPECT_EQ(buf->serial(), 3);
    EXPECT_FALSE(link.Pop().has_value());
}

TEST(MessageBusTest, DeliversInOrderAndShutdownWakesTheConsumer) {
    graph::MessageBus bus;
    bus.Post({graph::GraphMessageType::kFirstFrame, nullptr, 1, {}, 2.5});
    bus.Post({graph::GraphMessageType::kEos, nullptr, 1, {}});

    std::optional<graph::GraphMessage> first = bus.Wait();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->type, graph::GraphMessageType::kFirstFrame);
    EXPECT_DOUBLE_EQ(first->pts, 2.5);
    std::optional<graph::GraphMessage> second = bus.Wait();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->type, graph::GraphMessageType::kEos);

    auto waiting = std::async(std::launch::async, [&bus] { return bus.Wait(); });
    EXPECT_EQ(waiting.wait_for(50ms), std::future_status::timeout);
    bus.Shutdown();
    ASSERT_EQ(waiting.wait_for(2s), std::future_status::ready);
    EXPECT_FALSE(waiting.get().has_value());

    bus.Post({graph::GraphMessageType::kError, nullptr, 1, "late"});  // Dropped, never blocks.
    EXPECT_FALSE(bus.Wait().has_value());
}

// A frame decoded before a seek must not drag the clock back (ffplay's serial).
TEST(ClockTest, IgnoresUpdatesFromAnOlderEpoch) {
    Clock clock;
    clock.SetPaused(true);
    clock.Reset(10.0, 2);
    clock.Set(3.0, 1);
    EXPECT_NEAR(clock.Get(), 10.0, 1e-9);
    clock.Set(10.5, 2);
    EXPECT_NEAR(clock.Get(), 10.5, 1e-9);
}

TEST(ClockTest, ExtrapolatesWhileRunning) {
    Clock clock;
    clock.Reset(5.0, 1);
    clock.Set(5.0, 1);
    std::this_thread::sleep_for(100ms);
    const double now = clock.Get();
    EXPECT_GT(now, 5.05);
    EXPECT_LT(now, 5.5);
    clock.SetPaused(true);
    const double frozen = clock.Get();
    std::this_thread::sleep_for(50ms);
    EXPECT_DOUBLE_EQ(clock.Get(), frozen);
}

}  // namespace
}  // namespace mvp
