#include "imr_window.hpp"
#include <cmath>
#include <cassert>

IMRWindow::IMRWindow(std::size_t capacity)
    : values_(capacity, 0u), mrs_(capacity, 0.0), capacity_(capacity) {
    assert(capacity > 1);
}

void IMRWindow::push(uint32_t value) {
    values_[v_head_] = value;
    v_head_ = (v_head_ + 1) % capacity_;
    if (v_count_ < capacity_) ++v_count_;

    if (has_prev_) {
        double mr = std::abs(static_cast<double>(value) -
                             static_cast<double>(prev_));
        mrs_[mr_head_] = mr;
        mr_head_ = (mr_head_ + 1) % capacity_;
        if (mr_count_ < capacity_) ++mr_count_;
    }
    has_prev_ = true;
    prev_     = value;
}

std::size_t IMRWindow::size() const { return v_count_; }

bool IMRWindow::is_ready(std::size_t min) const { return v_count_ >= min; }

double IMRWindow::x_bar() const {
    if (v_count_ == 0) return 0.0;
    double sum = 0.0;
    for (std::size_t i = 0; i < v_count_; ++i)
        sum += static_cast<double>(values_[i]);
    return sum / static_cast<double>(v_count_);
}

double IMRWindow::mr_bar() const {
    if (mr_count_ == 0) return 0.0;
    double sum = 0.0;
    for (std::size_t i = 0; i < mr_count_; ++i)
        sum += mrs_[i];
    return sum / static_cast<double>(mr_count_);
}

double IMRWindow::sigma()  const { return mr_bar() / d2; }
double IMRWindow::ucl_i()  const { return x_bar() + 3.0 * sigma(); }
double IMRWindow::lcl_i()  const { return x_bar() - 3.0 * sigma(); }
double IMRWindow::ucl_mr() const { return D4 * mr_bar(); }
