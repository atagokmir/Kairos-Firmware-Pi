#include "imr_window.hpp"
#include <gtest/gtest.h>
#include <cmath>

// Known dataset: 10, 12, 11, 13, 12
// MRs: |12-10|=2, |11-12|=1, |13-11|=2, |12-13|=1
// x_bar = 58/5 = 11.6
// mr_bar = 6/4 = 1.5
// sigma = 1.5 / 1.128 = 1.32978...
// ucl_i = 11.6 + 3 * (1.5/1.128) = 11.6 + 3.98936... = 15.5893...
// lcl_i = 11.6 - 3 * (1.5/1.128) = 11.6 - 3.98936... = 7.6106...
// ucl_mr = 3.267 * 1.5 = 4.9005

TEST(IMRWindowTest, EmptyWindow) {
    IMRWindow w(5);
    EXPECT_EQ(w.size(), 0u);
    EXPECT_FALSE(w.is_ready(1));
}

TEST(IMRWindowTest, SizeTracking) {
    IMRWindow w(5);
    w.push(10);
    EXPECT_EQ(w.size(), 1u);
    w.push(12);
    EXPECT_EQ(w.size(), 2u);
}

TEST(IMRWindowTest, IsReady) {
    IMRWindow w(10);
    EXPECT_FALSE(w.is_ready(3));
    w.push(1); w.push(2);
    EXPECT_FALSE(w.is_ready(3));
    w.push(3);
    EXPECT_TRUE(w.is_ready(3));
}

TEST(IMRWindowTest, XBarKnownValues) {
    IMRWindow w(5);
    for (uint32_t v : {10u, 12u, 11u, 13u, 12u}) w.push(v);
    EXPECT_NEAR(w.x_bar(), 11.6, 1e-9);
}

TEST(IMRWindowTest, MRBarKnownValues) {
    IMRWindow w(5);
    for (uint32_t v : {10u, 12u, 11u, 13u, 12u}) w.push(v);
    // MRs: 2, 1, 2, 1 → mr_bar = 1.5
    EXPECT_NEAR(w.mr_bar(), 1.5, 1e-9);
}

TEST(IMRWindowTest, SigmaFromMRBar) {
    IMRWindow w(5);
    for (uint32_t v : {10u, 12u, 11u, 13u, 12u}) w.push(v);
    // sigma = 1.5 / 1.128
    EXPECT_NEAR(w.sigma(), 1.5 / 1.128, 1e-6);
}

TEST(IMRWindowTest, UCL_I) {
    IMRWindow w(5);
    for (uint32_t v : {10u, 12u, 11u, 13u, 12u}) w.push(v);
    double expected = 11.6 + 3.0 * (1.5 / 1.128);
    EXPECT_NEAR(w.ucl_i(), expected, 1e-6);
}

TEST(IMRWindowTest, LCL_I) {
    IMRWindow w(5);
    for (uint32_t v : {10u, 12u, 11u, 13u, 12u}) w.push(v);
    double expected = 11.6 - 3.0 * (1.5 / 1.128);
    EXPECT_NEAR(w.lcl_i(), expected, 1e-6);
}

TEST(IMRWindowTest, UCL_MR) {
    IMRWindow w(5);
    for (uint32_t v : {10u, 12u, 11u, 13u, 12u}) w.push(v);
    // ucl_mr = 3.267 * 1.5
    EXPECT_NEAR(w.ucl_mr(), 3.267 * 1.5, 1e-6);
}

TEST(IMRWindowTest, SingleValueNoMR) {
    IMRWindow w(5);
    w.push(1000);
    EXPECT_NEAR(w.x_bar(), 1000.0, 1e-9);
    EXPECT_NEAR(w.mr_bar(), 0.0, 1e-9);  // no MR yet
    EXPECT_NEAR(w.sigma(), 0.0, 1e-9);
}

TEST(IMRWindowTest, IdenticalValues) {
    IMRWindow w(5);
    for (int i = 0; i < 5; ++i) w.push(1000);
    EXPECT_NEAR(w.x_bar(), 1000.0, 1e-9);
    EXPECT_NEAR(w.mr_bar(), 0.0, 1e-9);   // all MRs are 0
    EXPECT_NEAR(w.sigma(), 0.0, 1e-9);
    EXPECT_NEAR(w.ucl_i(), 1000.0, 1e-9); // no spread → UCL = LCL = mean
    EXPECT_NEAR(w.lcl_i(), 1000.0, 1e-9);
}

TEST(IMRWindowTest, CircularOverwrite) {
    // Capacity 3, push 5 values — oldest 2 evicted
    IMRWindow w(3);
    w.push(10); w.push(12); w.push(11); w.push(13); w.push(12);
    EXPECT_EQ(w.size(), 3u);
}
