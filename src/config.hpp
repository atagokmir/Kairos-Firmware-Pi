#pragma once
#include <string>
#include <cstddef>

struct Config {
    std::string port              = "/dev/ttyACM0";
    std::size_t window_size       = 100;
    std::size_t min_samples       = 50;
    std::size_t summary_interval  = 50;
    std::string log_path          = "/var/log/kairos/kairos.log";
    double      anomaly_threshold = 3.0;
};

// Load from INI file. Missing keys use Config defaults.
// Returns default Config if file not found.
Config load_config_file(const std::string& path);

// Apply CLI args on top of existing cfg.
// Supports: --port, --window, --min-samples, --summary-interval, --log, --threshold
void apply_args(Config& cfg, int argc, char* argv[]);
