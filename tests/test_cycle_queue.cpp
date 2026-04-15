#include "cycle_queue.hpp"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>

TEST(CycleQueueTest, PushAndPop) {
    CycleQueue q;
    q.push(12345u);
    uint32_t val = 0;
    bool ok = q.wait_and_pop(val);
    EXPECT_TRUE(ok);
    EXPECT_EQ(val, 12345u);
}

TEST(CycleQueueTest, MultipleValues) {
    CycleQueue q;
    q.push(1u); q.push(2u); q.push(3u);
    uint32_t v;
    q.wait_and_pop(v); EXPECT_EQ(v, 1u);
    q.wait_and_pop(v); EXPECT_EQ(v, 2u);
    q.wait_and_pop(v); EXPECT_EQ(v, 3u);
}

TEST(CycleQueueTest, StopReturnsFalse) {
    CycleQueue q;
    std::thread stopper([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        q.stop();
    });
    uint32_t v = 0;
    bool ok = q.wait_and_pop(v);
    EXPECT_FALSE(ok);
    stopper.join();
}

TEST(CycleQueueTest, StopDrainsRemainingItems) {
    CycleQueue q;
    q.push(42u);
    q.stop();
    uint32_t v = 0;
    bool ok = q.wait_and_pop(v);
    EXPECT_TRUE(ok);
    EXPECT_EQ(v, 42u);
    // Now empty and stopped → false
    ok = q.wait_and_pop(v);
    EXPECT_FALSE(ok);
}
