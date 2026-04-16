#include <iostream>
#include <thread>
#include <atomic>
#include <csignal>
#include <string>
#include "config.hpp"
#include "cycle_queue.hpp"
#include "shared_state.hpp"
#include "logger.hpp"
#include "serial_thread.hpp"
#include "stats_thread.hpp"
#include "command_queue.hpp"

#ifdef KAIROS_DISPLAY
#include "display_thread.hpp"
#endif

static std::atomic<bool> g_running{true};

static void signal_handler(int) {
    g_running = false;
}

int main(int argc, char* argv[]) {
    // Look for --config before general arg parsing
    std::string config_path = "kairos.conf";
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--config") {
            config_path = argv[i + 1];
            break;
        }
    }

    Config cfg = load_config_file(config_path);
    apply_args(cfg, argc, argv);

    // Validate required non-zero config values
    if (cfg.summary_interval == 0 || cfg.window_size == 0 || cfg.min_samples == 0) {
        std::cerr << "[ERROR] summary_interval, window_size, and min_samples must be >= 1\n";
        return 1;
    }

    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    Logger       logger(cfg.log_path);
    CycleQueue   queue;
    SharedState  state;
    CommandQueue cmd_queue;

    logger.info("Kairos starting. port=" + cfg.port +
                " window=" + std::to_string(cfg.window_size) +
                " min_samples=" + std::to_string(cfg.min_samples) +
                " threshold=" + std::to_string(cfg.anomaly_threshold));

    std::thread serial_t(serial_thread_func,
                         std::cref(cfg), std::ref(queue),
                         std::ref(cmd_queue),
                         std::ref(logger), std::ref(g_running));

    std::thread stats_t(stats_thread_func,
                        std::cref(cfg), std::ref(queue),
                        std::ref(state), std::ref(logger),
                        std::ref(g_running));

#ifdef KAIROS_DISPLAY
    // On macOS, SDL2 must run on the main thread (Cocoa requirement).
    // Display runs here; serial and stats run in background threads above.
    display_thread_func(cfg, state, cmd_queue, logger, g_running);
    g_running = false;
    queue.stop();
#endif

    serial_t.join();
    stats_t.join();

    logger.info("Kairos stopped cleanly.");
    return 0;
}
