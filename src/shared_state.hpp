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
    double   ucl           = 0.0;
    double   lcl           = 0.0;
    uint32_t last_cycle    = 0;
    uint64_t cycle_count   = 0;
    uint64_t anomaly_count = 0;
    bool     warming_up    = true;
};
