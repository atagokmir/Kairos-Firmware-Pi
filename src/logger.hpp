#pragma once
#include <string>
#include <mutex>
#include <fstream>
#include <cstdint>

class Logger {
public:
    // log_path: file path. stdout_enabled: also print to stdout.
    explicit Logger(const std::string& log_path, bool stdout_enabled = true);

    void info(const std::string& msg);
    void warn(const std::string& msg);
    void anomaly(uint32_t cycle, double mean, double sigma,
                 double ucl, double lcl,
                 double mr, double ucl_mr);   // MR chart values
    void summary(uint64_t count, double mean, double sigma,
                 double ucl, double lcl, uint64_t anomaly_count);

private:
    void        write(const std::string& level, const std::string& msg);
    std::string timestamp();

    std::mutex    mtx_;
    std::ofstream file_;
    bool          stdout_enabled_;
};
