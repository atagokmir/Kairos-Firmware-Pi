#pragma once
#include <atomic>
#include "config.hpp"
#include "cycle_queue.hpp"
#include "shared_state.hpp"
#include "logger.hpp"

// Processes cycle values from queue, computes UCL/LCL, detects anomalies.
// Runs until queue.wait_and_pop() returns false (queue stopped).
void stats_thread_func(const Config&      cfg,
                       CycleQueue&        queue,
                       SharedState&       state,
                       Logger&            logger,
                       std::atomic<bool>& running);
