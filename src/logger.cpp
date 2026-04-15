#include "logger.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <filesystem>

Logger::Logger(const std::string& log_path, bool stdout_enabled)
    : stdout_enabled_(stdout_enabled) {
    try {
        auto dir = std::filesystem::path(log_path).parent_path();
        if (!dir.empty()) std::filesystem::create_directories(dir);
    } catch (...) {}

    file_.open(log_path, std::ios::app);
    if (!file_.is_open()) {
        std::cerr << "[WARN] Cannot open log file: " << log_path
                  << ". Stdout only.\n";
    }
}

std::string Logger::timestamp() {
    auto now = std::chrono::system_clock::now();
    auto t   = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << std::put_time(std::localtime(&t), "[%Y-%m-%d %H:%M:%S]");
    return ss.str();
}

void Logger::write(const std::string& level, const std::string& msg) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::string line = timestamp() + " [" + level + "] " + msg + "\n";
    if (stdout_enabled_) std::cout << line << std::flush;
    if (file_.is_open()) { file_ << line; file_.flush(); }
}

void Logger::info(const std::string& msg) { write("INFO", msg); }
void Logger::warn(const std::string& msg) { write("WARN", msg); }

void Logger::anomaly(uint32_t cycle, double mean, double sigma,
                     double ucl, double lcl) {
    std::ostringstream ss;
    ss << "cycle=" << cycle
       << "us mean=" << static_cast<uint64_t>(mean)
       << "us UCL=" << static_cast<uint64_t>(ucl)
       << "us LCL=" << static_cast<uint64_t>(lcl)
       << "us sigma=" << static_cast<uint64_t>(sigma) << "us";
    write("ANOMALY", ss.str());
}

void Logger::summary(uint64_t count, double mean, double sigma,
                     double ucl, double lcl, uint64_t anomaly_count) {
    std::ostringstream ss;
    ss << "count=" << count
       << " mean=" << static_cast<uint64_t>(mean) << "us"
       << " sigma=" << static_cast<uint64_t>(sigma) << "us"
       << " UCL=" << static_cast<uint64_t>(ucl) << "us"
       << " LCL=" << static_cast<uint64_t>(lcl) << "us"
       << " anomalies=" << anomaly_count;
    write("SUMMARY", ss.str());
}
