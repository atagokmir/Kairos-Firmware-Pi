#include "logger.hpp"
#include <gtest/gtest.h>
#include <fstream>
#include <string>

static std::string read_last_line(const std::string& path) {
    std::ifstream f(path);
    std::string line, last;
    while (std::getline(f, line)) last = line;
    return last;
}

TEST(LoggerTest, CreatesLogFile) {
    const std::string path = "/tmp/kairos_test_creates.log";
    std::remove(path.c_str());
    { Logger log(path, false); log.info("hello"); }
    std::ifstream f(path);
    EXPECT_TRUE(f.good());
}

TEST(LoggerTest, InfoContainsLevelAndMessage) {
    const std::string path = "/tmp/kairos_test_info.log";
    std::remove(path.c_str());
    { Logger log(path, false); log.info("test message"); }
    std::string line = read_last_line(path);
    EXPECT_NE(line.find("[INFO]"),       std::string::npos);
    EXPECT_NE(line.find("test message"), std::string::npos);
}

TEST(LoggerTest, WarnContainsLevel) {
    const std::string path = "/tmp/kairos_test_warn.log";
    std::remove(path.c_str());
    { Logger log(path, false); log.warn("port lost"); }
    std::string line = read_last_line(path);
    EXPECT_NE(line.find("[WARN]"),    std::string::npos);
    EXPECT_NE(line.find("port lost"), std::string::npos);
}

TEST(LoggerTest, AnomalyContainsAllFields) {
    const std::string path = "/tmp/kairos_test_anomaly.log";
    std::remove(path.c_str());
    { Logger log(path, false); log.anomaly(1250000, 1000000.0, 40000.0, 1120000.0, 880000.0, 50000.0, 200000.0); }
    std::string line = read_last_line(path);
    EXPECT_NE(line.find("[ANOMALY]"), std::string::npos);
    EXPECT_NE(line.find("1250000"),   std::string::npos);
    EXPECT_NE(line.find("1000000"),   std::string::npos);
    EXPECT_NE(line.find("1120000"),   std::string::npos);
    EXPECT_NE(line.find("880000"),    std::string::npos);
}

TEST(LoggerTest, SummaryContainsAllFields) {
    const std::string path = "/tmp/kairos_test_summary.log";
    std::remove(path.c_str());
    { Logger log(path, false); log.summary(150, 1000200.0, 39800.0, 1119600.0, 880800.0, 2); }
    std::string line = read_last_line(path);
    EXPECT_NE(line.find("[SUMMARY]"), std::string::npos);
    EXPECT_NE(line.find("150"),       std::string::npos);
    EXPECT_NE(line.find("2"),         std::string::npos);
}

TEST(LoggerTest, FallbackToStdoutIfDirMissing) {
    EXPECT_NO_THROW({
        Logger log("/nonexistent_dir_xyz/kairos.log", false);
        log.info("should not crash");
    });
}

TEST(LoggerTest, AppendNotOverwrite) {
    const std::string path = "/tmp/kairos_test_append.log";
    std::remove(path.c_str());
    { Logger log(path, false); log.info("first"); }
    { Logger log(path, false); log.info("second"); }
    std::ifstream f(path);
    int count = 0;
    std::string line;
    while (std::getline(f, line)) ++count;
    EXPECT_EQ(count, 2);
}
