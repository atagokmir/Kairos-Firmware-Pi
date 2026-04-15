#pragma once
#include <atomic>
#include "config.hpp"
#include "cycle_queue.hpp"
#include "logger.hpp"

// Reads CYCLE lines from serial port and pushes values to queue.
// Runs until running is set to false.
void serial_thread_func(const Config&      cfg,
                        CycleQueue&        queue,
                        Logger&            logger,
                        std::atomic<bool>& running);
