#pragma once
#include <atomic>
#include "config.hpp"
#include "cycle_queue.hpp"
#include "command_queue.hpp"
#include "logger.hpp"

// Reads CYCLE lines from serial port, pushes values to cycle_queue.
// Drains cmd_queue and writes outgoing commands (e.g. "START\n") to port.
// Runs until running is set to false.
void serial_thread_func(const Config&      cfg,
                        CycleQueue&        cycle_queue,
                        CommandQueue&      cmd_queue,
                        Logger&            logger,
                        std::atomic<bool>& running);
