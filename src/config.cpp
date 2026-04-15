#include "config.hpp"
#include <fstream>
#include <string>
#include <cctype>

static std::string trim(const std::string& s) {
    std::size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
        ++start;
    std::size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
        --end;
    return s.substr(start, end - start);
}

Config load_config_file(const std::string& path) {
    Config cfg;
    std::ifstream f(path);
    if (!f.is_open()) return cfg;

    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if      (key == "port")              cfg.port              = val;
        else if (key == "window_size")       cfg.window_size       = std::stoul(val);
        else if (key == "min_samples")       cfg.min_samples       = std::stoul(val);
        else if (key == "summary_interval")  cfg.summary_interval  = std::stoul(val);
        else if (key == "log_path")          cfg.log_path          = val;
        else if (key == "anomaly_threshold") cfg.anomaly_threshold = std::stod(val);
    }
    return cfg;
}

void apply_args(Config& cfg, int argc, char* argv[]) {
    for (int i = 1; i + 1 < argc; ++i) {
        std::string arg = argv[i];
        std::string val = argv[i + 1];
        if      (arg == "--port")             { cfg.port              = val;              ++i; }
        else if (arg == "--window")           { cfg.window_size       = std::stoul(val); ++i; }
        else if (arg == "--min-samples")      { cfg.min_samples       = std::stoul(val); ++i; }
        else if (arg == "--summary-interval") { cfg.summary_interval  = std::stoul(val); ++i; }
        else if (arg == "--log")              { cfg.log_path          = val;              ++i; }
        else if (arg == "--threshold")        { cfg.anomaly_threshold = std::stod(val);  ++i; }
    }
}
