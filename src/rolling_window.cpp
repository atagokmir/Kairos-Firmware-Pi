#include "rolling_window.hpp"
#include <cmath>
#include <cassert>

RollingWindow::RollingWindow(std::size_t capacity)
    : buffer_(capacity, 0u), capacity_(capacity) {
    assert(capacity > 0);
}

void RollingWindow::push(uint32_t value) {
    buffer_[head_] = value;
    head_ = (head_ + 1) % capacity_;
    if (count_ < capacity_) ++count_;
}

std::size_t RollingWindow::size()     const { return count_; }
std::size_t RollingWindow::capacity() const { return capacity_; }

bool RollingWindow::is_ready(std::size_t min_samples) const {
    return count_ >= min_samples;
}

double RollingWindow::mean() const {
    if (count_ == 0) return 0.0;
    double sum = 0.0;
    for (std::size_t i = 0; i < count_; ++i)
        sum += static_cast<double>(buffer_[i]);
    return sum / static_cast<double>(count_);
}

double RollingWindow::sigma() const {
    if (count_ < 2) return 0.0;
    double m  = mean();
    double sq = 0.0;
    for (std::size_t i = 0; i < count_; ++i) {
        double d = static_cast<double>(buffer_[i]) - m;
        sq += d * d;
    }
    return std::sqrt(sq / static_cast<double>(count_));
}

double RollingWindow::ucl(double threshold) const {
    return mean() + threshold * sigma();
}

double RollingWindow::lcl(double threshold) const {
    return mean() - threshold * sigma();
}
