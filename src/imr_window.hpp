#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

// I-MR Window: Individuals & Moving Range Chart calculations.
// Tracks the last `capacity` measurements and their consecutive differences.
// Used for SPC calibration and ongoing process monitoring.
class IMRWindow {
public:
    explicit IMRWindow(std::size_t capacity);  // capacity must be > 1

    void        push(uint32_t value);
    std::size_t size()                      const;
    bool        is_ready(std::size_t min)   const;

    double x_bar()  const;   // mean of individual values
    double mr_bar() const;   // mean of moving ranges
    double sigma()  const;   // mr_bar / d2  (unbiased process sigma estimate)
    double ucl_i()  const;   // x_bar + 3*sigma
    double lcl_i()  const;   // x_bar - 3*sigma
    double ucl_mr() const;   // D4 * mr_bar  (MR chart upper limit)
    // lcl_mr = 0 always (D3 = 0 for n=2)

private:
    static constexpr double d2 = 1.128;  // SPC constant, n=2
    static constexpr double D4 = 3.267;  // SPC constant, n=2

    std::vector<uint32_t> values_;   // circular buffer of individuals
    std::vector<double>   mrs_;      // circular buffer of moving ranges
    std::size_t v_head_   = 0;
    std::size_t v_count_  = 0;
    std::size_t mr_head_  = 0;
    std::size_t mr_count_ = 0;
    std::size_t capacity_;
    uint32_t    prev_     = 0;
    bool        has_prev_ = false;
};
