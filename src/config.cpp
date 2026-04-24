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
        else if (key == "window_size")       try { cfg.window_size       = std::stoul(val); } catch (...) {}
        else if (key == "min_samples")       try { cfg.min_samples       = std::stoul(val); } catch (...) {}
        else if (key == "summary_interval")  try { cfg.summary_interval  = std::stoul(val); } catch (...) {}
        else if (key == "log_path")          cfg.log_path          = val;
        else if (key == "anomaly_threshold") try { cfg.anomaly_threshold = std::stod(val);  } catch (...) {}
        else if (key == "machine_id")        cfg.machine_id        = val;
        else if (key == "line_id")           cfg.line_id           = val;
        else if (key == "idle_timeout_s")    try { cfg.idle_timeout_s    = std::stoi(val);  } catch (...) {}
        else if (key == "autostart")         cfg.autostart = (val == "1" || val == "true");
    }
    return cfg;
}

void apply_args(Config& cfg, int argc, char* argv[]) {
    for (int i = 1; i + 1 < argc; ++i) {
        std::string arg = argv[i];
        std::string val = argv[i + 1];
        if      (arg == "--port")             { cfg.port              = val;                                       ++i; }
        else if (arg == "--window")           { try { cfg.window_size       = std::stoul(val); } catch (...) {}   ++i; }
        else if (arg == "--min-samples")      { try { cfg.min_samples       = std::stoul(val); } catch (...) {}   ++i; }
        else if (arg == "--summary-interval") { try { cfg.summary_interval  = std::stoul(val); } catch (...) {}   ++i; }
        else if (arg == "--log")              { cfg.log_path          = val;                                       ++i; }
        else if (arg == "--threshold")        { try { cfg.anomaly_threshold = std::stod(val);  } catch (...) {}   ++i; }
        else if (arg == "--autostart")        { cfg.autostart = true; }
    }
}
