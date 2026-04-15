#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

class RollingWindow {
public:
    explicit RollingWindow(std::size_t capacity);

    void        push(uint32_t value);
    std::size_t size()     const;
    std::size_t capacity() const;
    bool        is_ready(std::size_t min_samples) const;
    double      mean()     const;
    double      sigma()    const;             // population std dev
    double      ucl(double threshold) const; // mean + threshold*sigma
    double      lcl(double threshold) const; // mean - threshold*sigma

private:
    std::vector<uint32_t> buffer_;
    std::size_t           head_     = 0;
    std::size_t           count_    = 0;
    std::size_t           capacity_;
};
