#include "config.hpp"
#include <gtest/gtest.h>
#include <fstream>

TEST(ConfigTest, DefaultValues) {
    Config cfg;
    EXPECT_EQ(cfg.port, "/dev/ttyACM0");
    EXPECT_EQ(cfg.window_size, 100u);
    EXPECT_EQ(cfg.min_samples, 50u);
    EXPECT_EQ(cfg.summary_interval, 50u);
    EXPECT_EQ(cfg.log_path, "/var/log/kairos/kairos.log");
    EXPECT_DOUBLE_EQ(cfg.anomaly_threshold, 3.0);
}

TEST(ConfigTest, LoadFromFile) {
    const std::string path = "/tmp/kairos_test.conf";
    {
        std::ofstream f(path);
        f << "port=/dev/ttyUSB0\n"
          << "window_size=200\n"
          << "min_samples=75\n"
          << "summary_interval=25\n"
          << "log_path=/tmp/kairos_test.log\n"
          << "anomaly_threshold=2.5\n";
    }
    Config cfg = load_config_file(path);
    EXPECT_EQ(cfg.port, "/dev/ttyUSB0");
    EXPECT_EQ(cfg.window_size, 200u);
    EXPECT_EQ(cfg.min_samples, 75u);
    EXPECT_EQ(cfg.summary_interval, 25u);
    EXPECT_EQ(cfg.log_path, "/tmp/kairos_test.log");
    EXPECT_DOUBLE_EQ(cfg.anomaly_threshold, 2.5);
}

TEST(ConfigTest, MissingFileUsesDefaults) {
    Config cfg = load_config_file("/nonexistent/path/kairos.conf");
    EXPECT_EQ(cfg.port, "/dev/ttyACM0");
}

TEST(ConfigTest, CommentsAndEmptyLinesIgnored) {
    const std::string path = "/tmp/kairos_comments.conf";
    {
        std::ofstream f(path);
        f << "# this is a comment\n"
          << "\n"
          << "port=/dev/ttyUSB1\n"
          << "# another comment\n";
    }
    Config cfg = load_config_file(path);
    EXPECT_EQ(cfg.port, "/dev/ttyUSB1");
    EXPECT_EQ(cfg.window_size, 100u);  // default
}

TEST(ConfigTest, ApplyCLIArgs) {
    Config cfg;
    const char* args[] = {
        "kairos", "--port", "/dev/ttyUSB1",
        "--window", "50", "--threshold", "2.0"
    };
    apply_args(cfg, 7, const_cast<char**>(args));
    EXPECT_EQ(cfg.port, "/dev/ttyUSB1");
    EXPECT_EQ(cfg.window_size, 50u);
    EXPECT_DOUBLE_EQ(cfg.anomaly_threshold, 2.0);
}

TEST(ConfigTest, CLIDoesNotOverrideUnspecified) {
    Config cfg;
    const char* args[] = {"kairos", "--port", "/dev/ttyUSB2"};
    apply_args(cfg, 3, const_cast<char**>(args));
    EXPECT_EQ(cfg.port, "/dev/ttyUSB2");
    EXPECT_EQ(cfg.window_size, 100u);  // unchanged default
}
