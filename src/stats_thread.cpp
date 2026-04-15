#include "stats_thread.hpp"
#include "rolling_window.hpp"
#include <string>

void stats_thread_func(const Config&      cfg,
                       CycleQueue&        queue,
                       SharedState&       state,
                       Logger&            logger,
                       std::atomic<bool>& running) {
    RollingWindow window(cfg.window_size);
    uint64_t cycle_count   = 0;
    uint64_t anomaly_count = 0;

    (void)running;  // stats_thread exits when queue is stopped
    while (true) {
        uint32_t cycle = 0;
        if (!queue.wait_and_pop(cycle)) break;  // queue stopped

        window.push(cycle);
        ++cycle_count;

        if (!window.is_ready(cfg.min_samples)) {
            logger.info("Warming up... samples=" +
                        std::to_string(window.size()) + "/" +
                        std::to_string(cfg.min_samples));
            // Still update cycle count in SharedState so readers can see progress
            {
                std::unique_lock lock(state.mtx);
                state.last_cycle  = cycle;
                state.cycle_count = cycle_count;
                // warming_up stays true (default)
            }
            continue;
        }

        const double mean  = window.mean();
        const double sigma = window.sigma();
        const double ucl   = window.ucl(cfg.anomaly_threshold);
        const double lcl   = window.lcl(cfg.anomaly_threshold);

        if (static_cast<double>(cycle) > ucl || static_cast<double>(cycle) < lcl) {
            ++anomaly_count;
            logger.anomaly(cycle, mean, sigma, ucl, lcl);
        }

        if (cycle_count % cfg.summary_interval == 0) {
            logger.summary(cycle_count, mean, sigma, ucl, lcl, anomaly_count);
        }

        // Update SharedState (brief exclusive lock)
        {
            std::unique_lock lock(state.mtx);
            state.mean          = mean;
            state.sigma         = sigma;
            state.ucl           = ucl;
            state.lcl           = lcl;
            state.last_cycle    = cycle;
            state.cycle_count   = cycle_count;
            state.anomaly_count = anomaly_count;
            state.warming_up    = false;
        }
    }
}
