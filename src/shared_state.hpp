#pragma once
#include <shared_mutex>
#include <cstdint>

// Statistics shared between threads.
// Writers (StatsThread): std::unique_lock
// Readers (DisplayThread, MQTTThread): std::shared_lock — concurrent reads allowed
struct SharedState {
    mutable std::shared_mutex mtx;

    double   mean          = 0.0;
    double   sigma         = 0.0;
    double   ucl           = 0.0;   // UCL_I (I chart upper control limit)
    double   lcl           = 0.0;   // LCL_I (I chart lower control limit)
    double   ucl_mr        = 0.0;   // UCL_MR (MR chart upper control limit)
    double   mr_bar        = 0.0;   // mean moving range
    uint32_t last_cycle    = 0;
    uint64_t cycle_count   = 0;
    uint64_t anomaly_count = 0;
    bool     warming_up    = true;
    bool     limits_locked = false; // true once UCL/LCL are finalized (local or server)
};
