#include "rolling_window.hpp"
#include <gtest/gtest.h>
#include <cmath>

TEST(RollingWindowTest, EmptyWindow) {
    RollingWindow w(5);
    EXPECT_EQ(w.size(), 0u);
    EXPECT_EQ(w.capacity(), 5u);
    EXPECT_FALSE(w.is_ready(1));
}

TEST(RollingWindowTest, PushAndSize) {
    RollingWindow w(5);
    w.push(1000);
    EXPECT_EQ(w.size(), 1u);
    w.push(2000);
    EXPECT_EQ(w.size(), 2u);
}

TEST(RollingWindowTest, DoesNotExceedCapacity) {
    RollingWindow w(3);
    w.push(100); w.push(200); w.push(300);
    EXPECT_EQ(w.size(), 3u);
    w.push(400);  // oldest (100) evicted
    EXPECT_EQ(w.size(), 3u);
}

TEST(RollingWindowTest, MeanOfIdenticalValues) {
    RollingWindow w(5);
    w.push(1000000); w.push(1000000); w.push(1000000);
    EXPECT_DOUBLE_EQ(w.mean(), 1000000.0);
}

TEST(RollingWindowTest, MeanOfMixedValues) {
    RollingWindow w(4);
    w.push(1000); w.push(2000); w.push(3000); w.push(4000);
    EXPECT_DOUBLE_EQ(w.mean(), 2500.0);
}

TEST(RollingWindowTest, SigmaZeroForIdentical) {
    RollingWindow w(5);
    w.push(1000); w.push(1000); w.push(1000);
    EXPECT_DOUBLE_EQ(w.sigma(), 0.0);
}

TEST(RollingWindowTest, SigmaKnownValues) {
    // Values: 2, 4, 4, 4, 5, 5, 7, 9 → mean=5, population sigma=2
    RollingWindow w(8);
    for (uint32_t v : {2u, 4u, 4u, 4u, 5u, 5u, 7u, 9u}) w.push(v);
    EXPECT_NEAR(w.mean(), 5.0, 1e-9);
    EXPECT_NEAR(w.sigma(), 2.0, 1e-9);
}

TEST(RollingWindowTest, UCLandLCL) {
    // mean=5, sigma=2, threshold=3.0 → UCL=11, LCL=-1
    RollingWindow w(8);
    for (uint32_t v : {2u, 4u, 4u, 4u, 5u, 5u, 7u, 9u}) w.push(v);
    EXPECT_NEAR(w.ucl(3.0), 11.0, 1e-9);
    EXPECT_NEAR(w.lcl(3.0), -1.0, 1e-9);
}

TEST(RollingWindowTest, IsReadyThreshold) {
    RollingWindow w(10);
    EXPECT_FALSE(w.is_ready(5));
    for (int i = 0; i < 4; ++i) w.push(1000);
    EXPECT_FALSE(w.is_ready(5));
    w.push(1000);
    EXPECT_TRUE(w.is_ready(5));
}

TEST(RollingWindowTest, RollingMeanAfterWrap) {
    // Window size 3: push 100, 200, 300, 400 (400 replaces 100)
    // Active values: 400, 200, 300 → mean = 300
    RollingWindow w(3);
    w.push(100); w.push(200); w.push(300); w.push(400);
    EXPECT_NEAR(w.mean(), 300.0, 1e-9);
}
