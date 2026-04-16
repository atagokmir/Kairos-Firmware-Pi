#include "display_thread.hpp"

#include <lvgl.h>
#include <string>
#include <deque>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdio>

#ifdef KAIROS_SDL2
#  include <drivers/sdl/lv_sdl_window.h>
#  include <drivers/sdl/lv_sdl_mouse.h>
#  include <drivers/sdl/lv_sdl_mousewheel.h>
#endif

// ─── Constants ────────────────────────────────────────────────────────────────
static constexpr int SCREEN_W   = 1024;
static constexpr int SCREEN_H   = 600;
static constexpr int CHART_POINTS = 50;
static constexpr int REFRESH_MS = 33;   // ~30fps

// Chart scale: show values in ms (divide µs by 1000)
static constexpr int32_t SCALE = 1000;

// ─── Colors ───────────────────────────────────────────────────────────────────
static constexpr uint32_t CLR_BG       = 0x0D1117;  // dark background
static constexpr uint32_t CLR_PANEL    = 0x161B22;  // panel background
static constexpr uint32_t CLR_BORDER   = 0x30363D;  // panel border
static constexpr uint32_t CLR_TEXT     = 0xE6EDF3;  // primary text
static constexpr uint32_t CLR_MUTED    = 0x8B949E;  // secondary text
static constexpr uint32_t CLR_NORMAL   = 0x3FB950;  // green
static constexpr uint32_t CLR_ANOMALY  = 0xF85149;  // red
static constexpr uint32_t CLR_WARMING  = 0xD29922;  // yellow/orange
static constexpr uint32_t CLR_CHART    = 0x58A6FF;  // blue
static constexpr uint32_t CLR_LIMIT    = 0xF85149;  // red for UCL/LCL

// ─── Widget handles ───────────────────────────────────────────────────────────
static lv_obj_t *g_status_dot   = nullptr;
static lv_obj_t *g_status_label = nullptr;
static lv_obj_t *g_cycle_label  = nullptr;
static lv_obj_t *g_ucl_label    = nullptr;
static lv_obj_t *g_mean_label   = nullptr;
static lv_obj_t *g_lcl_label    = nullptr;
static lv_obj_t *g_sigma_label  = nullptr;
static lv_obj_t *g_prod_label   = nullptr;
static lv_obj_t *g_anom_label   = nullptr;
static lv_obj_t *g_state_label  = nullptr;
static lv_obj_t *g_chart        = nullptr;

static lv_chart_series_t *g_data_ser = nullptr;
static lv_chart_series_t *g_ucl_ser  = nullptr;
static lv_chart_series_t *g_lcl_ser  = nullptr;

// ─── Helpers ──────────────────────────────────────────────────────────────────
static std::string fmt_us(uint32_t us) {
    // Format with thousands separator, e.g. 1,002,847 µs
    char buf[32];
    if (us >= 1000000u) {
        uint32_t s = us / 1000000u;
        uint32_t ms = (us % 1000000u) / 1000u;
        uint32_t us_r = us % 1000u;
        std::snprintf(buf, sizeof(buf), "%u.%03u.%03u \xc2\xb5s", s, ms, us_r);
    } else if (us >= 1000u) {
        uint32_t ms = us / 1000u;
        uint32_t us_r = us % 1000u;
        std::snprintf(buf, sizeof(buf), "%u.%03u \xc2\xb5s", ms, us_r);
    } else {
        std::snprintf(buf, sizeof(buf), "%u \xc2\xb5s", us);
    }
    return buf;
}

static std::string fmt_double_us(double us) {
    return fmt_us(static_cast<uint32_t>(us < 0.0 ? 0.0 : us));
}

static void style_panel(lv_obj_t *obj) {
    lv_obj_set_style_bg_color(obj, lv_color_hex(CLR_PANEL), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_set_style_pad_all(obj, 12, 0);
}

// ─── UI Creation ──────────────────────────────────────────────────────────────
static void create_ui() {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(CLR_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // ── Header bar ──────────────────────────────────────────────────────────
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, SCREEN_W, 58);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(CLR_PANEL), 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(header, lv_color_hex(CLR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(header, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_pad_hor(header, 20, 0);
    lv_obj_set_style_pad_ver(header, 0, 0);

    // Title
    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "KAIROS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    // Status dot + label
    g_status_dot = lv_obj_create(header);
    lv_obj_set_size(g_status_dot, 12, 12);
    lv_obj_set_style_radius(g_status_dot, 6, 0);
    lv_obj_set_style_border_width(g_status_dot, 0, 0);
    lv_obj_set_style_bg_color(g_status_dot, lv_color_hex(CLR_WARMING), 0);
    lv_obj_align(g_status_dot, LV_ALIGN_RIGHT_MID, -110, 0);

    g_status_label = lv_label_create(header);
    lv_label_set_text(g_status_label, "ISINIYOR");
    lv_obj_set_style_text_font(g_status_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_status_label, lv_color_hex(CLR_WARMING), 0);
    lv_obj_align(g_status_label, LV_ALIGN_RIGHT_MID, -10, 0);

    // ── Top row: current cycle | control limits ──────────────────────────────
    // Left panel — current cycle
    lv_obj_t *left = lv_obj_create(scr);
    lv_obj_set_pos(left, 4, 62);
    lv_obj_set_size(left, 380, 190);
    style_panel(left);

    lv_obj_t *cycle_title = lv_label_create(left);
    lv_label_set_text(cycle_title, "SON CYCLE");
    lv_obj_set_style_text_font(cycle_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(cycle_title, lv_color_hex(CLR_MUTED), 0);
    lv_obj_align(cycle_title, LV_ALIGN_TOP_LEFT, 0, 0);

    g_cycle_label = lv_label_create(left);
    lv_label_set_text(g_cycle_label, "---");
    lv_obj_set_style_text_font(g_cycle_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(g_cycle_label, lv_color_hex(CLR_TEXT), 0);
    lv_obj_align(g_cycle_label, LV_ALIGN_CENTER, 0, 10);

    // Right panel — control limits
    lv_obj_t *right = lv_obj_create(scr);
    lv_obj_set_pos(right, 388, 62);
    lv_obj_set_size(right, 632, 190);
    style_panel(right);

    lv_obj_t *limits_title = lv_label_create(right);
    lv_label_set_text(limits_title, "KONTROL L\xc4\xb0M\xc4\xb0TLER\xc4\xb0");
    lv_obj_set_style_text_font(limits_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(limits_title, lv_color_hex(CLR_MUTED), 0);
    lv_obj_align(limits_title, LV_ALIGN_TOP_LEFT, 0, 0);

    // UCL
    lv_obj_t *ucl_key = lv_label_create(right);
    lv_label_set_text(ucl_key, "UCL");
    lv_obj_set_style_text_font(ucl_key, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ucl_key, lv_color_hex(CLR_LIMIT), 0);
    lv_obj_align(ucl_key, LV_ALIGN_TOP_LEFT, 0, 28);

    g_ucl_label = lv_label_create(right);
    lv_label_set_text(g_ucl_label, "---");
    lv_obj_set_style_text_font(g_ucl_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_ucl_label, lv_color_hex(CLR_TEXT), 0);
    lv_obj_align(g_ucl_label, LV_ALIGN_TOP_RIGHT, 0, 28);

    // Mean
    lv_obj_t *mean_key = lv_label_create(right);
    lv_label_set_text(mean_key, "X\xcc\x84");  // X̄
    lv_obj_set_style_text_font(mean_key, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(mean_key, lv_color_hex(CLR_CHART), 0);
    lv_obj_align(mean_key, LV_ALIGN_TOP_LEFT, 0, 58);

    g_mean_label = lv_label_create(right);
    lv_label_set_text(g_mean_label, "---");
    lv_obj_set_style_text_font(g_mean_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_mean_label, lv_color_hex(CLR_TEXT), 0);
    lv_obj_align(g_mean_label, LV_ALIGN_TOP_RIGHT, 0, 58);

    // LCL
    lv_obj_t *lcl_key = lv_label_create(right);
    lv_label_set_text(lcl_key, "LCL");
    lv_obj_set_style_text_font(lcl_key, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lcl_key, lv_color_hex(CLR_LIMIT), 0);
    lv_obj_align(lcl_key, LV_ALIGN_TOP_LEFT, 0, 88);

    g_lcl_label = lv_label_create(right);
    lv_label_set_text(g_lcl_label, "---");
    lv_obj_set_style_text_font(g_lcl_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_lcl_label, lv_color_hex(CLR_TEXT), 0);
    lv_obj_align(g_lcl_label, LV_ALIGN_TOP_RIGHT, 0, 88);

    // Sigma
    lv_obj_t *sigma_key = lv_label_create(right);
    lv_label_set_text(sigma_key, "\xcf\x83");  // σ
    lv_obj_set_style_text_font(sigma_key, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(sigma_key, lv_color_hex(CLR_MUTED), 0);
    lv_obj_align(sigma_key, LV_ALIGN_TOP_LEFT, 0, 118);

    g_sigma_label = lv_label_create(right);
    lv_label_set_text(g_sigma_label, "---");
    lv_obj_set_style_text_font(g_sigma_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_sigma_label, lv_color_hex(CLR_MUTED), 0);
    lv_obj_align(g_sigma_label, LV_ALIGN_TOP_RIGHT, 0, 118);

    // ── Chart ───────────────────────────────────────────────────────────────
    lv_obj_t *chart_panel = lv_obj_create(scr);
    lv_obj_set_pos(chart_panel, 4, 256);
    lv_obj_set_size(chart_panel, 1016, 246);
    style_panel(chart_panel);
    lv_obj_set_style_pad_all(chart_panel, 8, 0);

    g_chart = lv_chart_create(chart_panel);
    lv_obj_set_size(g_chart, 996, 226);
    lv_obj_align(g_chart, LV_ALIGN_CENTER, 0, 0);
    lv_chart_set_type(g_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(g_chart, CHART_POINTS);
    lv_chart_set_range(g_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 2000);
    lv_obj_set_style_bg_color(g_chart, lv_color_hex(CLR_PANEL), 0);
    lv_obj_set_style_border_width(g_chart, 0, 0);
    lv_obj_set_style_size(g_chart, 0, 0, LV_PART_INDICATOR);  // hide dots
    lv_obj_set_style_line_width(g_chart, 2, LV_PART_ITEMS);

    // Data series (blue)
    g_data_ser = lv_chart_add_series(g_chart,
        lv_color_hex(CLR_CHART), LV_CHART_AXIS_PRIMARY_Y);

    // UCL reference line (red, dashed look via thin line)
    g_ucl_ser = lv_chart_add_series(g_chart,
        lv_color_hex(CLR_LIMIT), LV_CHART_AXIS_PRIMARY_Y);

    // LCL reference line (red)
    g_lcl_ser = lv_chart_add_series(g_chart,
        lv_color_hex(CLR_LIMIT), LV_CHART_AXIS_PRIMARY_Y);

    // Initialize UCL/LCL series with LV_CHART_POINT_NONE (hidden until locked)
    lv_value_precise_t * ucl_pts = lv_chart_get_y_array(g_chart, g_ucl_ser);
    lv_value_precise_t * lcl_pts = lv_chart_get_y_array(g_chart, g_lcl_ser);
    for (int i = 0; i < CHART_POINTS; i++) {
        ucl_pts[i] = LV_CHART_POINT_NONE;
        lcl_pts[i] = LV_CHART_POINT_NONE;
    }

    // ── Bottom bar ──────────────────────────────────────────────────────────
    lv_obj_t *bottom = lv_obj_create(scr);
    lv_obj_set_pos(bottom, 4, 506);
    lv_obj_set_size(bottom, 1016, 90);
    style_panel(bottom);

    // Production count
    lv_obj_t *prod_panel = lv_obj_create(bottom);
    lv_obj_set_size(prod_panel, 310, 66);
    lv_obj_align(prod_panel, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_opa(prod_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(prod_panel, 0, 0);
    lv_obj_set_style_pad_all(prod_panel, 0, 0);

    lv_obj_t *prod_key = lv_label_create(prod_panel);
    lv_label_set_text(prod_key, "\xc3\x9cret\xc4\xb0m");  // Üretim
    lv_obj_set_style_text_font(prod_key, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(prod_key, lv_color_hex(CLR_MUTED), 0);
    lv_obj_align(prod_key, LV_ALIGN_TOP_LEFT, 0, 0);

    g_prod_label = lv_label_create(prod_panel);
    lv_label_set_text(g_prod_label, "0 adet");
    lv_obj_set_style_text_font(g_prod_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(g_prod_label, lv_color_hex(CLR_TEXT), 0);
    lv_obj_align(g_prod_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    // Anomaly count
    lv_obj_t *anom_panel = lv_obj_create(bottom);
    lv_obj_set_size(anom_panel, 310, 66);
    lv_obj_align(anom_panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(anom_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(anom_panel, 0, 0);
    lv_obj_set_style_pad_all(anom_panel, 0, 0);

    lv_obj_t *anom_key = lv_label_create(anom_panel);
    lv_label_set_text(anom_key, "Anomali");
    lv_obj_set_style_text_font(anom_key, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(anom_key, lv_color_hex(CLR_MUTED), 0);
    lv_obj_align(anom_key, LV_ALIGN_TOP_LEFT, 0, 0);

    g_anom_label = lv_label_create(anom_panel);
    lv_label_set_text(g_anom_label, "0");
    lv_obj_set_style_text_font(g_anom_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(g_anom_label, lv_color_hex(CLR_TEXT), 0);
    lv_obj_align(g_anom_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    // Status panel
    lv_obj_t *state_panel = lv_obj_create(bottom);
    lv_obj_set_size(state_panel, 310, 66);
    lv_obj_align(state_panel, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_opa(state_panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(state_panel, 0, 0);
    lv_obj_set_style_pad_all(state_panel, 0, 0);

    lv_obj_t *state_key = lv_label_create(state_panel);
    lv_label_set_text(state_key, "Durum");
    lv_obj_set_style_text_font(state_key, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(state_key, lv_color_hex(CLR_MUTED), 0);
    lv_obj_align(state_key, LV_ALIGN_TOP_LEFT, 0, 0);

    g_state_label = lv_label_create(state_panel);
    lv_label_set_text(g_state_label, "BEKLENIYOR");
    lv_obj_set_style_text_font(g_state_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_state_label, lv_color_hex(CLR_WARMING), 0);
    lv_obj_align(g_state_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

// ─── UI Update ────────────────────────────────────────────────────────────────
struct UISnapshot {
    uint32_t last_cycle;
    uint64_t cycle_count;
    uint64_t anomaly_count;
    double   mean;
    double   sigma;
    double   ucl;
    double   lcl;
    bool     warming_up;
    bool     limits_locked;
};

static bool g_limits_drawn = false;
static uint64_t g_last_cycle_count = 0;

static void update_ui(const UISnapshot& s) {
    // ── Cycle label ────────────────────────────────────────────────────────
    if (s.cycle_count > 0) {
        lv_label_set_text(g_cycle_label, fmt_us(s.last_cycle).c_str());
    }

    // ── Status indicator ──────────────────────────────────────────────────
    if (s.warming_up) {
        lv_obj_set_style_bg_color(g_status_dot, lv_color_hex(CLR_WARMING), 0);
        lv_obj_set_style_text_color(g_status_label, lv_color_hex(CLR_WARMING), 0);
        lv_label_set_text(g_status_label, "ISINIYOR");
        lv_obj_set_style_text_color(g_state_label, lv_color_hex(CLR_WARMING), 0);
        lv_label_set_text(g_state_label, "KALIBRASYON");
    } else {
        // Check if last cycle was anomaly
        bool is_anomaly = s.limits_locked && s.cycle_count > 0 &&
                          (static_cast<double>(s.last_cycle) > s.ucl ||
                           (s.lcl > 0.0 && static_cast<double>(s.last_cycle) < s.lcl));
        if (is_anomaly) {
            lv_obj_set_style_bg_color(g_status_dot, lv_color_hex(CLR_ANOMALY), 0);
            lv_obj_set_style_text_color(g_status_label, lv_color_hex(CLR_ANOMALY), 0);
            lv_label_set_text(g_status_label, "ANOMAL\xc4\xb0");  // ANOMALİ
            lv_obj_set_style_text_color(g_state_label, lv_color_hex(CLR_ANOMALY), 0);
            lv_label_set_text(g_state_label, "KONTROL D\xc4\xb0\xc5\x9e\xc4\xb0");  // KONTROL DIŞI
        } else {
            lv_obj_set_style_bg_color(g_status_dot, lv_color_hex(CLR_NORMAL), 0);
            lv_obj_set_style_text_color(g_status_label, lv_color_hex(CLR_NORMAL), 0);
            lv_label_set_text(g_status_label, "NORMAL");
            lv_obj_set_style_text_color(g_state_label, lv_color_hex(CLR_NORMAL), 0);
            lv_label_set_text(g_state_label, "KONTROL\xc4\xb0\xc7\x87\xc4\xb0NDE");  // KONTROLÜNDE (approx)
        }
    }

    // ── Control limits ────────────────────────────────────────────────────
    if (s.limits_locked) {
        lv_label_set_text(g_ucl_label, fmt_double_us(s.ucl).c_str());
        lv_label_set_text(g_mean_label, fmt_double_us(s.mean).c_str());
        lv_label_set_text(g_lcl_label, fmt_double_us(s.lcl < 0 ? 0 : s.lcl).c_str());
        lv_label_set_text(g_sigma_label, fmt_double_us(s.sigma).c_str());
    }

    // ── Counters ──────────────────────────────────────────────────────────
    lv_label_set_text(g_prod_label, (std::to_string(s.cycle_count) + " adet").c_str());
    lv_label_set_text(g_anom_label, std::to_string(s.anomaly_count).c_str());

    // ── Chart ─────────────────────────────────────────────────────────────
    if (s.cycle_count > g_last_cycle_count && s.cycle_count > 0) {
        g_last_cycle_count = s.cycle_count;

        // Scale to ms for chart
        int32_t val_ms = static_cast<int32_t>(s.last_cycle / SCALE);
        lv_chart_set_next_value(g_chart, g_data_ser, val_ms);

        // Update Y range dynamically around mean once calibrated
        if (s.limits_locked) {
            int32_t ucl_ms = static_cast<int32_t>(s.ucl / SCALE);
            int32_t lcl_ms = static_cast<int32_t>(s.lcl < 0 ? 0 : s.lcl / SCALE);
            int32_t margin = std::max(50, (ucl_ms - lcl_ms) / 4);
            lv_chart_set_range(g_chart, LV_CHART_AXIS_PRIMARY_Y,
                               lcl_ms - margin, ucl_ms + margin);

            // Draw UCL/LCL reference lines (once)
            if (!g_limits_drawn) {
                lv_value_precise_t * ucl_pts = lv_chart_get_y_array(g_chart, g_ucl_ser);
                lv_value_precise_t * lcl_pts = lv_chart_get_y_array(g_chart, g_lcl_ser);
                for (int i = 0; i < CHART_POINTS; i++) {
                    ucl_pts[i] = static_cast<lv_value_precise_t>(ucl_ms);
                    lcl_pts[i] = static_cast<lv_value_precise_t>(lcl_ms);
                }
                g_limits_drawn = true;
            }
        }

        lv_chart_refresh(g_chart);
    }
}

// ─── Platform init ────────────────────────────────────────────────────────────
static void platform_init() {
#ifdef KAIROS_SDL2
    lv_display_t * disp = lv_sdl_window_create(SCREEN_W, SCREEN_H);
    (void)disp;
    lv_sdl_mouse_create();
    lv_sdl_mousewheel_create();
#endif
#ifdef KAIROS_FBDEV
    lv_display_t * disp = lv_linux_fbdev_create();
    lv_linux_fbdev_set_file(disp, "/dev/fb0");
#endif
}

// ─── Thread entry ─────────────────────────────────────────────────────────────
void display_thread_func(const Config&      cfg,
                         SharedState&       state,
                         Logger&            logger,
                         std::atomic<bool>& running) {
    (void)cfg;

    lv_init();
    platform_init();
    create_ui();

    logger.info("Display thread started.");

    auto last_tick = std::chrono::steady_clock::now();

    while (running.load()) {
        // Update LVGL tick
        auto now = std::chrono::steady_clock::now();
        uint32_t elapsed = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - last_tick).count());
        if (elapsed > 0) {
            lv_tick_inc(elapsed);
            last_tick = now;
        }

        // Read shared state
        UISnapshot snap{};
        {
            std::shared_lock lock(state.mtx);
            snap.last_cycle    = state.last_cycle;
            snap.cycle_count   = state.cycle_count;
            snap.anomaly_count = state.anomaly_count;
            snap.mean          = state.mean;
            snap.sigma         = state.sigma;
            snap.ucl           = state.ucl;
            snap.lcl           = state.lcl;
            snap.warming_up    = state.warming_up;
            snap.limits_locked = state.limits_locked;
        }

        update_ui(snap);
        lv_timer_handler();

        std::this_thread::sleep_for(std::chrono::milliseconds(REFRESH_MS));
    }

    logger.info("Display thread stopped.");
}
