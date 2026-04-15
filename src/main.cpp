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

    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    Logger      logger(cfg.log_path);
    CycleQueue  queue;
    SharedState state;

    logger.info("Kairos starting. port=" + cfg.port +
                " window=" + std::to_string(cfg.window_size) +
                " min_samples=" + std::to_string(cfg.min_samples) +
                " threshold=" + std::to_string(cfg.anomaly_threshold));

    std::thread serial_t(serial_thread_func,
                         std::cref(cfg), std::ref(queue),
                         std::ref(logger), std::ref(g_running));

    std::thread stats_t(stats_thread_func,
                        std::cref(cfg), std::ref(queue),
                        std::ref(state), std::ref(logger),
                        std::ref(g_running));

    serial_t.join();
    stats_t.join();

    logger.info("Kairos stopped cleanly.");
    return 0;
}
