#include "stats_thread.hpp"
#include "imr_window.hpp"
#include <string>
#include <cmath>

void stats_thread_func(const Config&      cfg,
                       CycleQueue&        queue,
                       SharedState&       state,
                       Logger&            logger,
                       std::atomic<bool>& running) {
    IMRWindow window(cfg.window_size);
    uint64_t  cycle_count   = 0;
    uint64_t  anomaly_count = 0;
    bool      limits_set    = false;  // has this thread finalized the limits?
    uint32_t  prev_cycle    = 0;
    bool      has_prev      = false;

    (void)running;  // stats_thread exits when queue is stopped
    while (true) {
        uint32_t cycle = 0;
        if (!queue.wait_and_pop(cycle)) break;

        window.push(cycle);
        ++cycle_count;

        // Calibration phase
        if (!window.is_ready(cfg.min_samples)) {
            logger.info("Warming up... samples=" +
                        std::to_string(window.size()) + "/" +
                        std::to_string(cfg.min_samples));
            {
                std::unique_lock lock(state.mtx);
                state.last_cycle  = cycle;
                state.cycle_count = cycle_count;
            }
            prev_cycle = cycle;
            has_prev   = true;
            continue;
        }

        // Calibration complete — lock limits (once)
        if (!limits_set) {
            // Check if server already provided limits
            { std::shared_lock lock(state.mtx); limits_set = state.limits_locked; }

            if (!limits_set) {
                const double xb    = window.x_bar();
                const double mrb   = window.mr_bar();
                const double sig   = window.sigma();
                const double ucl_i = window.ucl_i();
                const double lcl_i = window.lcl_i();
                const double ucl_m = window.ucl_mr();

                {
                    std::unique_lock lock(state.mtx);
                    state.mean          = xb;
                    state.sigma         = sig;
                    state.ucl           = ucl_i;
                    state.lcl           = lcl_i;
                    state.ucl_mr        = ucl_m;
                    state.mr_bar        = mrb;
                    state.warming_up    = false;
                    state.limits_locked = true;
                }
                limits_set = true;

                logger.info("I-MR limits locked:"
                    " X_bar=" + std::to_string(static_cast<uint64_t>(xb)) + "us"
                    " UCL=" + std::to_string(static_cast<uint64_t>(ucl_i)) + "us"
                    " LCL=" + std::to_string(static_cast<uint64_t>(lcl_i)) + "us"
                    " MR_bar=" + std::to_string(static_cast<uint64_t>(mrb)) + "us"
                    " UCL_MR=" + std::to_string(static_cast<uint64_t>(ucl_m)) + "us");
            } else {
                std::unique_lock lock(state.mtx);
                state.warming_up = false;
            }
        }

        // Read fixed limits
        double ucl, lcl, ucl_mr_val;
        {
            std::shared_lock lock(state.mtx);
            ucl        = state.ucl;
            lcl        = state.lcl;
            ucl_mr_val = state.ucl_mr;
        }

        // Anomaly detection — I chart and MR chart
        const double cycled = static_cast<double>(cycle);
        const double mr     = has_prev
                              ? std::abs(cycled - static_cast<double>(prev_cycle))
                              : 0.0;

        const bool i_violation  = (cycled > ucl) || (lcl > 0.0 && cycled < lcl);
        const bool mr_violation = has_prev && (mr > ucl_mr_val);

        if (i_violation || mr_violation) {
            ++anomaly_count;
            logger.anomaly(cycle, window.x_bar(), window.sigma(),
                           ucl, lcl, mr, ucl_mr_val);
        }

        if (cycle_count % cfg.summary_interval == 0) {
            logger.summary(cycle_count, window.x_bar(), window.sigma(),
                           ucl, lcl, anomaly_count);
        }

        // Update live stats — UCL/LCL unchanged after lock
        {
            std::unique_lock lock(state.mtx);
            state.mean          = window.x_bar();
            state.sigma         = window.sigma();
            state.mr_bar        = window.mr_bar();
            state.last_cycle    = cycle;
            state.cycle_count   = cycle_count;
            state.anomaly_count = anomaly_count;
        }

        prev_cycle = cycle;
        has_prev   = true;
    }
}
