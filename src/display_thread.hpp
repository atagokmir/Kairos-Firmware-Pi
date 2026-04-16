#pragma once
#include <atomic>
#include "config.hpp"
#include "shared_state.hpp"
#include "command_queue.hpp"
#include "logger.hpp"

// Manages LVGL HMI. States: SCREENSAVER → PRE_PROD → DETAIL ↔ IDLE.
// Pushes "START\n" to cmd_queue when user presses the start button.
// On macOS/SDL2: must run on the main thread (Cocoa requirement).
void display_thread_func(const Config&      cfg,
                         SharedState&       state,
                         CommandQueue&      cmd_queue,
                         Logger&            logger,
                         std::atomic<bool>& running);
