#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <cstdint>

class CycleQueue {
public:
    void push(uint32_t value) {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            queue_.push(value);
        }
        cv_.notify_one();
    }

    // Blocks until a value is available or stop() is called.
    // Returns false if stopped and queue is empty.
    bool wait_and_pop(uint32_t& value) {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [&] { return !queue_.empty() || stopped_; });
        if (queue_.empty()) return false;
        value = queue_.front();
        queue_.pop();
        return true;
    }

    // Wake all blocked wait_and_pop calls; they return false when queue is empty.
    void stop() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            stopped_ = true;
        }
        cv_.notify_all();
    }

private:
    std::queue<uint32_t>    queue_;
    std::mutex              mtx_;
    std::condition_variable cv_;
    bool                    stopped_ = false;
};
