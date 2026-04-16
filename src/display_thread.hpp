#pragma once
#include <atomic>
#include "config.hpp"
#include "shared_state.hpp"
#include "logger.hpp"

// Reads SharedState and renders the LVGL UI.
// Runs until running is set to false.
// Must only be used when compiled with KAIROS_SDL2 or KAIROS_FBDEV.
void display_thread_func(const Config&      cfg,
                         SharedState&       state,
                         Logger&            logger,
                         std::atomic<bool>& running);
