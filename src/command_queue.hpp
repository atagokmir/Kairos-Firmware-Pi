#pragma once
#include <queue>
#include <string>
#include <mutex>

// Thread-safe queue for outgoing serial commands (Pi → Pico).
// display_thread pushes commands; serial_thread drains and writes them.
class CommandQueue {
public:
    void push(std::string cmd) {
        std::lock_guard<std::mutex> lock(mtx_);
        q_.push(std::move(cmd));
    }

    // Non-blocking pop. Returns true if a command was available.
    bool try_pop(std::string &out) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (q_.empty()) return false;
        out = std::move(q_.front());
        q_.pop();
        return true;
    }

private:
    std::queue<std::string> q_;
    std::mutex              mtx_;
};
