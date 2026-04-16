/**
 * display_thread.cpp — Kairos HMI
 *
 * States:
 *   SCREENSAVER: animated logo, touch → PRE_PROD
 *   PRE_PROD:    machine info + "URETIME BASLA" button → sends START\n, go to DETAIL
 *   DETAIL:      full I/MR dashboard
 *   IDLE:        simplified big-status view (after idle_timeout_s of no touch)
 *
 * LVGL tick runs in a dedicated 1ms thread.
 * lv_timer_handler() runs in the caller's thread (must be main thread on macOS/SDL2).
 */

#include "display_thread.hpp"
#include "command_queue.hpp"

#include <lvgl.h>
#include <string>
#include <cstdio>
#include <cmath>
#include <ctime>
#include <atomic>
#include <thread>
#include <chrono>
#include <algorithm>

#ifdef KAIROS_SDL2
#  include <drivers/sdl/lv_sdl_window.h>
#  include <drivers/sdl/lv_sdl_mouse.h>
#  include <drivers/sdl/lv_sdl_mousewheel.h>
#endif
#ifdef KAIROS_FBDEV
#  include <drivers/linux/lv_linux_fbdev.h>
#endif

// ─── Screen ───────────────────────────────────────────────────────────────────
static constexpr int  W  = 1024;
static constexpr int  H  = 600;
static constexpr int  HDR_H = 48;       // header height
static constexpr int  CONTENT_Y = HDR_H + 4;
static constexpr int  CONTENT_H = H - CONTENT_Y;  // 548
static constexpr int  SIDEBAR_W = 288;
static constexpr int  CHART_POINTS = 56;

// ─── Colors ───────────────────────────────────────────────────────────────────
static constexpr uint32_t C_BG      = 0x0e1117;
static constexpr uint32_t C_PANEL   = 0x13181f;
static constexpr uint32_t C_BORDER  = 0x21262d;
static constexpr uint32_t C_TEXT    = 0xe6edf3;
static constexpr uint32_t C_MUTED   = 0x8b949e;
static constexpr uint32_t C_GREEN   = 0x3fb950;
static constexpr uint32_t C_RED     = 0xf85149;
static constexpr uint32_t C_AMBER   = 0xe3b341;
static constexpr uint32_t C_BLUE    = 0x79c0ff;

// ─── Mode ─────────────────────────────────────────────────────────────────────
enum class Mode { SCREENSAVER, PRE_PROD, DETAIL, IDLE };
static Mode g_mode = Mode::SCREENSAVER;  // always start in screensaver

// ─── Global widget handles ────────────────────────────────────────────────────
// Header (shared between DETAIL and IDLE modes)
static lv_obj_t *g_header        = nullptr;
static lv_obj_t *g_machine_lbl   = nullptr;
static lv_obj_t *g_clock_lbl     = nullptr;
static lv_obj_t *g_badge_box     = nullptr;
static lv_obj_t *g_badge_lbl     = nullptr;

// Detail view container
static lv_obj_t *g_detail        = nullptr;

// Sidebar: Son Cycle
static lv_obj_t *g_cycle_lbl     = nullptr;
static lv_obj_t *g_delta_lbl     = nullptr;

// Sidebar: Ortalama
static lv_obj_t *g_mean_live_lbl = nullptr;
static lv_obj_t *g_sigma_live_lbl= nullptr;

// Sidebar: Kontrol Limitleri
static lv_obj_t *g_ucl_lbl       = nullptr;
static lv_obj_t *g_calib_m_lbl   = nullptr;
static lv_obj_t *g_lcl_lbl       = nullptr;
static lv_obj_t *g_calib_s_lbl   = nullptr;

// Sidebar: Kalibrasyon
static lv_obj_t *g_kalib_info    = nullptr;
static lv_obj_t *g_kalib_bar     = nullptr;
static lv_obj_t *g_kalib_status  = nullptr;

// Charts
static lv_obj_t *g_i_chart       = nullptr;
static lv_obj_t *g_i_ort_lbl     = nullptr;   // "ort: Xs · s: Xs"
static lv_obj_t *g_mr_chart      = nullptr;
static lv_obj_t *g_mr_ucl_lbl    = nullptr;

static lv_chart_series_t *g_i_data  = nullptr;
static lv_chart_series_t *g_i_ucl   = nullptr;
static lv_chart_series_t *g_i_lcl   = nullptr;
static lv_chart_series_t *g_mr_data = nullptr;
static lv_chart_series_t *g_mr_ucl  = nullptr;

// Bottom bar
static lv_obj_t *g_prod_lbl      = nullptr;
static lv_obj_t *g_anom_d_lbl    = nullptr;

// Command queue pointer (set in display_thread_func, used by switch functions)
static CommandQueue *g_cmd_queue  = nullptr;

// Screensaver view
static lv_obj_t *g_screensaver   = nullptr;

// Pre-production view
static lv_obj_t *g_preprod       = nullptr;
static bool      g_start_pressed = false;
static bool      g_stop_pressed  = false;

// Idle view container
static lv_obj_t *g_idle          = nullptr;
static lv_obj_t *g_idle_box      = nullptr;
static lv_obj_t *g_idle_status   = nullptr;
static lv_obj_t *g_idle_sub      = nullptr;
static lv_obj_t *g_idle_cycle    = nullptr;
static lv_obj_t *g_idle_anom     = nullptr;
static lv_obj_t *g_idle_prod     = nullptr;

// ─── State ────────────────────────────────────────────────────────────────────
static double   g_calib_mean  = 0.0;
static double   g_calib_sigma = 0.0;
static bool     g_calib_saved = false;
static uint64_t g_last_count  = 0;
static uint32_t g_prev_cycle  = 0;
static bool     g_has_prev    = false;
static bool     g_limits_drawn= false;
static bool g_any_press = false;  // set by indev event callback
static void on_any_press(lv_event_t *) { g_any_press = true; }
static bool     g_was_anomaly = false;

// ─── Helpers ──────────────────────────────────────────────────────────────────
static std::string fmt_sec(uint32_t us) {
    char b[24]; std::snprintf(b, sizeof(b), "%.2f s", us / 1e6); return b;
}
static std::string fmt_dsec(double us) {
    char b[24]; std::snprintf(b, sizeof(b), "%.2f s", (us<0?0:us)/1e6); return b;
}

static void panel(lv_obj_t *o, uint32_t bg = C_PANEL, int radius = 8) {
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_pad_all(o, 10, 0);
}

static lv_obj_t *cont(lv_obj_t *parent) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *lbl(lv_obj_t *parent, const char *text,
                     const lv_font_t *font, uint32_t color) {
    lv_obj_t *o = lv_label_create(parent);
    lv_label_set_text(o, text);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    return o;
}

// Key-value row inside a panel (right-aligned value label)
static lv_obj_t *kv(lv_obj_t *parent, const char *key, uint32_t kc,
                    int y, lv_obj_t **val_out,
                    const lv_font_t *font = &lv_font_montserrat_14) {
    lv_obj_t *k = lbl(parent, key, font, kc);
    lv_obj_align(k, LV_ALIGN_TOP_LEFT, 0, y);
    lv_obj_t *v = lbl(parent, "---", font, C_TEXT);
    lv_obj_align(v, LV_ALIGN_TOP_RIGHT, 0, y);
    *val_out = v;
    return k;
}

static void series_const(lv_obj_t *chart, lv_chart_series_t *ser, int32_t val) {
    lv_value_precise_t *pts = lv_chart_get_y_array(chart, ser);
    for (int i = 0; i < CHART_POINTS; ++i) pts[i] = val;
    lv_chart_refresh(chart);
}


static void on_stop_btn(lv_event_t *) { g_stop_pressed = true; }

// ─── Header (always visible) ──────────────────────────────────────────────────
static void create_header(const Config &cfg) {
    g_header = lv_obj_create(lv_screen_active());
    lv_obj_t *hdr = g_header;
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_size(hdr, W, HDR_H);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(C_PANEL), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(hdr, lv_color_hex(C_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(hdr, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_hor(hdr, 16, 0);
    lv_obj_set_style_pad_ver(hdr, 0, 0);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    // KAIROS title
    lv_obj_t *title = lbl(hdr, "KAIROS", &lv_font_montserrat_24, C_TEXT);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    // Separator + machine info
    std::string info = " |  " + cfg.machine_id + "  |  " + cfg.line_id;
    g_machine_lbl = lbl(hdr, info.c_str(), &lv_font_montserrat_14, C_MUTED);
    lv_obj_align(g_machine_lbl, LV_ALIGN_LEFT_MID, 110, 0);

    // Clock
    g_clock_lbl = lbl(hdr, "00:00:00", &lv_font_montserrat_20, C_MUTED);
    lv_obj_align(g_clock_lbl, LV_ALIGN_RIGHT_MID, -340, 0);

    // "URETIM BITTI" button — visible only in DETAIL/IDLE, handled in state machine
    lv_obj_t *stop_btn = lv_obj_create(hdr);
    lv_obj_set_size(stop_btn, 152, 32);
    lv_obj_align(stop_btn, LV_ALIGN_RIGHT_MID, -172, 0);
    lv_obj_set_style_bg_color(stop_btn, lv_color_hex(0x200a0a), 0);
    lv_obj_set_style_bg_opa(stop_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(stop_btn, lv_color_hex(C_RED), 0);
    lv_obj_set_style_border_width(stop_btn, 1, 0);
    lv_obj_set_style_radius(stop_btn, 6, 0);
    lv_obj_set_style_pad_all(stop_btn, 0, 0);
    lv_obj_remove_flag(stop_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(stop_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(stop_btn, on_stop_btn, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *stop_lbl = lbl(stop_btn, "URETIM BITTI", &lv_font_montserrat_14, C_RED);
    lv_obj_align(stop_lbl, LV_ALIGN_CENTER, 0, 0);

    // Status badge
    g_badge_box = lv_obj_create(hdr);
    lv_obj_set_size(g_badge_box, 155, 32);
    lv_obj_align(g_badge_box, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(g_badge_box, lv_color_hex(0x0d1f0f), 0);
    lv_obj_set_style_bg_opa(g_badge_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(g_badge_box, lv_color_hex(C_GREEN), 0);
    lv_obj_set_style_border_width(g_badge_box, 2, 0);
    lv_obj_set_style_radius(g_badge_box, 6, 0);
    lv_obj_set_style_pad_all(g_badge_box, 0, 0);
    lv_obj_remove_flag(g_badge_box, LV_OBJ_FLAG_SCROLLABLE);

    g_badge_lbl = lv_label_create(g_badge_box);
    lv_label_set_text(g_badge_lbl, "ISINIYOR");
    lv_obj_set_style_text_font(g_badge_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_badge_lbl, lv_color_hex(C_GREEN), 0);
    lv_obj_align(g_badge_lbl, LV_ALIGN_CENTER, 0, 0);
}

// ─── Detail View ──────────────────────────────────────────────────────────────
static void create_detail(const Config &cfg) {
    // Full-screen transparent container below header
    g_detail = cont(lv_screen_active());
    lv_obj_set_pos(g_detail, 4, CONTENT_Y);
    lv_obj_set_size(g_detail, W-8, CONTENT_H);

    // ── Left sidebar ──────────────────────────────────────────────────────
    lv_obj_t *sb = cont(g_detail);
    lv_obj_set_pos(sb, 0, 0);
    lv_obj_set_size(sb, SIDEBAR_W, CONTENT_H);

    // Card 1: Son Cycle
    lv_obj_t *c1 = lv_obj_create(sb);
    lv_obj_set_pos(c1, 0, 0);
    lv_obj_set_size(c1, SIDEBAR_W-4, 126);
    panel(c1);

    lv_obj_t *ct1 = lbl(c1, "SON CYCLE", &lv_font_montserrat_14, C_MUTED);
    lv_obj_align(ct1, LV_ALIGN_TOP_LEFT, 0, 0);

    g_cycle_lbl = lbl(c1, "---", &lv_font_montserrat_32, C_TEXT);
    lv_obj_align(g_cycle_lbl, LV_ALIGN_LEFT_MID, 0, 4);

    g_delta_lbl = lbl(c1, "", &lv_font_montserrat_14, C_MUTED);
    lv_obj_align(g_delta_lbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    // Card 2: Ortalama (live)
    lv_obj_t *c2 = lv_obj_create(sb);
    lv_obj_set_pos(c2, 0, 130);
    lv_obj_set_size(c2, SIDEBAR_W-4, 98);
    panel(c2);

    lv_obj_t *ct2 = lbl(c2, "ORTALAMA", &lv_font_montserrat_14, C_MUTED);
    lv_obj_align(ct2, LV_ALIGN_TOP_LEFT, 0, 0);

    g_mean_live_lbl = lbl(c2, "---", &lv_font_montserrat_24, C_TEXT);
    lv_obj_align(g_mean_live_lbl, LV_ALIGN_LEFT_MID, 0, 4);

    g_sigma_live_lbl = lbl(c2, "", &lv_font_montserrat_14, C_MUTED);
    lv_obj_align(g_sigma_live_lbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    // Card 3: Kontrol Limitleri (calibration — fixed)
    lv_obj_t *c3 = lv_obj_create(sb);
    lv_obj_set_pos(c3, 0, 232);
    lv_obj_set_size(c3, SIDEBAR_W-4, 168);
    panel(c3);

    lv_obj_t *ct3 = lbl(c3, "KONTROL LIMITLERI", &lv_font_montserrat_14, C_MUTED);
    lv_obj_align(ct3, LV_ALIGN_TOP_LEFT, 0, 0);

    kv(c3, "UCL",   C_RED,   20, &g_ucl_lbl);
    kv(c3, "MEAN",  C_GREEN, 52, &g_calib_m_lbl);
    kv(c3, "LCL",   C_RED,   84, &g_lcl_lbl);
    kv(c3, "SIGMA", C_AMBER, 116, &g_calib_s_lbl);

    // Card 4: Kalibrasyon
    lv_obj_t *c4 = lv_obj_create(sb);
    lv_obj_set_pos(c4, 0, 404);
    lv_obj_set_size(c4, SIDEBAR_W-4, CONTENT_H-404);
    panel(c4);

    lv_obj_t *ct4 = lbl(c4, "KALIBRASYON", &lv_font_montserrat_14, C_MUTED);
    lv_obj_align(ct4, LV_ALIGN_TOP_LEFT, 0, 0);

    char kbuf[64];
    std::snprintf(kbuf, sizeof(kbuf), "%zu ornek  |  sabit limit",
                  cfg.min_samples);
    g_kalib_info = lbl(c4, kbuf, &lv_font_montserrat_14, C_MUTED);
    lv_obj_align(g_kalib_info, LV_ALIGN_TOP_LEFT, 0, 22);

    g_kalib_bar = lv_bar_create(c4);
    lv_obj_set_size(g_kalib_bar, SIDEBAR_W-28, 8);
    lv_obj_align(g_kalib_bar, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_bar_set_range(g_kalib_bar, 0, (int32_t)cfg.min_samples);
    lv_bar_set_value(g_kalib_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g_kalib_bar, lv_color_hex(0x1a2a1a), 0);
    lv_obj_set_style_bg_color(g_kalib_bar, lv_color_hex(C_GREEN), LV_PART_INDICATOR);

    g_kalib_status = lbl(c4, "ISINIYOR...", &lv_font_montserrat_14, C_AMBER);
    lv_obj_align(g_kalib_status, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    // ── Right area ────────────────────────────────────────────────────────
    int rx = SIDEBAR_W + 4;
    int rw = W - 8 - rx;  // = 1016 - 292 = 724

    // I Chart panel
    lv_obj_t *ip = lv_obj_create(g_detail);
    lv_obj_set_pos(ip, rx, 0);
    lv_obj_set_size(ip, rw, 204);
    panel(ip);
    lv_obj_set_style_pad_all(ip, 8, 0);
    lv_obj_remove_flag(ip, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *i_title = lbl(ip, "I CHART  |  BIREYSEL DEGERLER", &lv_font_montserrat_14, C_MUTED);
    lv_obj_align(i_title, LV_ALIGN_TOP_LEFT, 0, 0);
    g_i_ort_lbl = lbl(ip, "ort: ---  |  s: ---", &lv_font_montserrat_14, C_MUTED);
    lv_obj_align(g_i_ort_lbl, LV_ALIGN_TOP_RIGHT, 0, 0);

    g_i_chart = lv_chart_create(ip);
    lv_obj_set_size(g_i_chart, rw-18, 162);
    lv_obj_set_pos(g_i_chart, 0, 22);  // below title row
    lv_chart_set_type(g_i_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(g_i_chart, CHART_POINTS);
    lv_chart_set_range(g_i_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 2000);
    lv_obj_set_style_bg_color(g_i_chart, lv_color_hex(C_PANEL), 0);
    lv_obj_set_style_border_width(g_i_chart, 0, 0);
    // Show small dots on data points
    lv_obj_set_style_size(g_i_chart, 4, 4, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(g_i_chart, 2, LV_PART_ITEMS);

    g_i_data = lv_chart_add_series(g_i_chart, lv_color_hex(0xd0d7de), LV_CHART_AXIS_PRIMARY_Y);
    g_i_ucl  = lv_chart_add_series(g_i_chart, lv_color_hex(C_RED),    LV_CHART_AXIS_PRIMARY_Y);
    g_i_lcl  = lv_chart_add_series(g_i_chart, lv_color_hex(C_BLUE),   LV_CHART_AXIS_PRIMARY_Y);

    // Init reference lines as hidden
    auto *pu = lv_chart_get_y_array(g_i_chart, g_i_ucl);
    auto *pl = lv_chart_get_y_array(g_i_chart, g_i_lcl);
    for (int i = 0; i < CHART_POINTS; ++i) { pu[i] = LV_CHART_POINT_NONE; pl[i] = LV_CHART_POINT_NONE; }

    // MR Chart panel
    lv_obj_t *mrp = lv_obj_create(g_detail);
    lv_obj_set_pos(mrp, rx, 208);
    lv_obj_set_size(mrp, rw, 136);
    panel(mrp);
    lv_obj_set_style_pad_all(mrp, 8, 0);
    lv_obj_remove_flag(mrp, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *mr_title = lbl(mrp, "MR CHART  |  HAREKETLI ARALIK", &lv_font_montserrat_14, C_MUTED);
    lv_obj_align(mr_title, LV_ALIGN_TOP_LEFT, 0, 0);
    g_mr_ucl_lbl = lbl(mrp, "UCL_MR: ---", &lv_font_montserrat_14, C_RED);
    lv_obj_align(g_mr_ucl_lbl, LV_ALIGN_TOP_RIGHT, 0, 0);

    g_mr_chart = lv_chart_create(mrp);
    lv_obj_set_size(g_mr_chart, rw-18, 96);
    lv_obj_set_pos(g_mr_chart, 0, 22);  // below title row
    lv_chart_set_type(g_mr_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(g_mr_chart, CHART_POINTS);
    lv_chart_set_range(g_mr_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 500);
    lv_obj_set_style_bg_color(g_mr_chart, lv_color_hex(C_PANEL), 0);
    lv_obj_set_style_border_width(g_mr_chart, 0, 0);
    lv_obj_set_style_size(g_mr_chart, 4, 4, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(g_mr_chart, 2, LV_PART_ITEMS);

    g_mr_data = lv_chart_add_series(g_mr_chart, lv_color_hex(C_AMBER), LV_CHART_AXIS_PRIMARY_Y);
    g_mr_ucl  = lv_chart_add_series(g_mr_chart, lv_color_hex(C_RED),   LV_CHART_AXIS_PRIMARY_Y);
    auto *pm = lv_chart_get_y_array(g_mr_chart, g_mr_ucl);
    for (int i = 0; i < CHART_POINTS; ++i) pm[i] = LV_CHART_POINT_NONE;

    // Bottom stats bar (4 cards)
    int by = 208 + 136 + 4;
    int bh = CONTENT_H - by;  // remaining height
    int cw = (rw - 3*4) / 4;  // card width

    auto make_stat = [&](int idx, const char *key, uint32_t vc,
                         lv_obj_t **val_lbl) {
        lv_obj_t *card = lv_obj_create(g_detail);
        lv_obj_set_pos(card, rx + idx*(cw+4), by);
        lv_obj_set_size(card, cw, bh);
        panel(card);

        lv_obj_t *k = lbl(card, key, &lv_font_montserrat_14, C_MUTED);
        lv_obj_align(k, LV_ALIGN_TOP_MID, 0, 0);

        if (val_lbl) {
            lv_obj_t *v = lbl(card, "0", &lv_font_montserrat_32, vc);
            lv_obj_align(v, LV_ALIGN_CENTER, 0, 8);
            *val_lbl = v;
        } else {
            lv_obj_t *v = lbl(card, "---", &lv_font_montserrat_32, C_MUTED);
            lv_obj_align(v, LV_ALIGN_CENTER, 0, 8);
        }
    };

    make_stat(0, "URETIM",      C_TEXT,  &g_prod_lbl);
    make_stat(1, "ANOMALI",     C_RED,   &g_anom_d_lbl);
    make_stat(2, "HEDEF CYCLE", C_MUTED, nullptr);  // --- future MQTT
    make_stat(3, "OEE",         C_AMBER, nullptr);  // --- future
}

// ─── Idle View ────────────────────────────────────────────────────────────────
static void create_idle() {
    g_idle = cont(lv_screen_active());
    lv_obj_set_pos(g_idle, 4, CONTENT_Y);
    lv_obj_set_size(g_idle, W-8, CONTENT_H);
    lv_obj_add_flag(g_idle, LV_OBJ_FLAG_HIDDEN);

    // Big status box
    g_idle_box = lv_obj_create(g_idle);
    lv_obj_set_pos(g_idle_box, 0, 0);
    lv_obj_set_size(g_idle_box, W-8, 276);
    lv_obj_set_style_bg_color(g_idle_box, lv_color_hex(0x0d1f0f), 0);
    lv_obj_set_style_bg_opa(g_idle_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(g_idle_box, lv_color_hex(C_GREEN), 0);
    lv_obj_set_style_border_width(g_idle_box, 2, 0);
    lv_obj_set_style_radius(g_idle_box, 10, 0);
    lv_obj_set_style_pad_all(g_idle_box, 0, 0);
    lv_obj_remove_flag(g_idle_box, LV_OBJ_FLAG_SCROLLABLE);

    g_idle_status = lbl(g_idle_box, "ISINIYOR", &lv_font_montserrat_48, C_GREEN);
    lv_obj_align(g_idle_status, LV_ALIGN_CENTER, 0, -16);

    g_idle_sub = lbl(g_idle_box, "TUM PARAMETRELER NORMAL ARALIKTA", &lv_font_montserrat_14, 0x4a7d55);
    lv_obj_align(g_idle_sub, LV_ALIGN_CENTER, 0, 36);

    // 3 stat cards
    int cy = 280;
    int ch = CONTENT_H - cy - 44;  // leave 44 for footer
    int cw3 = (W-8 - 8) / 3;       // 3 cards with 2*4 gaps

    auto idle_card = [&](int idx, const char *title, lv_obj_t **val_out) {
        lv_obj_t *c = lv_obj_create(g_idle);
        lv_obj_set_pos(c, idx*(cw3+4), cy);
        lv_obj_set_size(c, cw3, ch);
        panel(c);
        lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *t = lbl(c, title, &lv_font_montserrat_14, C_MUTED);
        lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 0);

        lv_obj_t *v = lbl(c, "---", &lv_font_montserrat_48, C_GREEN);
        lv_obj_align(v, LV_ALIGN_CENTER, 0, 8);
        *val_out = v;
    };

    idle_card(0, "SON CYCLE",  &g_idle_cycle);
    idle_card(1, "ANOMALI",    &g_idle_anom);
    idle_card(2, "URETIM",     &g_idle_prod);

    // Footer
    lv_obj_t *footer = lbl(g_idle, "v   detay icin dokunun", &lv_font_montserrat_14, C_MUTED);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, 0);
}

// ─── Animation callbacks ──────────────────────────────────────────────────────
static void anim_y_cb(void *obj, int32_t v) {
    lv_obj_set_y((lv_obj_t *)obj, v);
}
static void anim_opa_cb(void *obj, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}
static void anim_w_cb(void *obj, int32_t v) {
    lv_obj_set_width((lv_obj_t *)obj, v);
    lv_obj_align((lv_obj_t *)obj, LV_ALIGN_CENTER, 0, 34);
}

// ─── Screensaver view ─────────────────────────────────────────────────────────
static void create_screensaver(const Config &cfg) {
    lv_obj_t *scr = lv_screen_active();

    g_screensaver = cont(scr);
    lv_obj_set_pos(g_screensaver, 0, 0);
    lv_obj_set_size(g_screensaver, W, H);
    lv_obj_set_style_bg_color(g_screensaver, lv_color_hex(0x080c14), 0);
    lv_obj_set_style_bg_opa(g_screensaver, LV_OPA_COVER, 0);

    // KAIROS logo
    lv_obj_t *logo = lbl(g_screensaver, "KAIROS", &lv_font_montserrat_48, C_GREEN);
    lv_obj_align(logo, LV_ALIGN_CENTER, 0, -60);
    int32_t logo_y = lv_obj_get_y(logo);

    // Float animation
    lv_anim_t af;
    lv_anim_init(&af);
    lv_anim_set_var(&af, logo);
    lv_anim_set_exec_cb(&af, anim_y_cb);
    lv_anim_set_values(&af, logo_y - 14, logo_y + 14);
    lv_anim_set_time(&af, 3200);
    lv_anim_set_playback_time(&af, 3200);
    lv_anim_set_repeat_count(&af, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&af, lv_anim_path_ease_in_out);
    lv_anim_start(&af);

    // Opacity pulse
    lv_anim_t ao;
    lv_anim_init(&ao);
    lv_anim_set_var(&ao, logo);
    lv_anim_set_exec_cb(&ao, anim_opa_cb);
    lv_anim_set_values(&ao, 140, 255);
    lv_anim_set_time(&ao, 2400);
    lv_anim_set_playback_time(&ao, 2400);
    lv_anim_set_delay(&ao, 400);
    lv_anim_set_repeat_count(&ao, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&ao, lv_anim_path_ease_in_out);
    lv_anim_start(&ao);

    // Animated accent line below logo
    lv_obj_t *line = lv_obj_create(g_screensaver);
    lv_obj_set_size(line, 0, 2);
    lv_obj_align(line, LV_ALIGN_CENTER, 0, 34);
    lv_obj_set_style_bg_color(line, lv_color_hex(C_GREEN), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 1, 0);

    lv_anim_t aw;
    lv_anim_init(&aw);
    lv_anim_set_var(&aw, line);
    lv_anim_set_exec_cb(&aw, anim_w_cb);
    lv_anim_set_values(&aw, 0, 220);
    lv_anim_set_time(&aw, 2000);
    lv_anim_set_playback_time(&aw, 2000);
    lv_anim_set_delay(&aw, 800);
    lv_anim_set_repeat_count(&aw, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&aw, lv_anim_path_ease_in_out);
    lv_anim_start(&aw);

    // Machine info
    std::string minfo = cfg.machine_id + "  |  " + cfg.line_id;
    lv_obj_t *mach = lbl(g_screensaver, minfo.c_str(), &lv_font_montserrat_20, 0x2d3f50);
    lv_obj_align(mach, LV_ALIGN_CENTER, 0, 70);

    // Touch hint (slow fade)
    lv_obj_t *hint = lbl(g_screensaver, "ekrana dokunun", &lv_font_montserrat_14, 0x1a2a3a);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -24);

    lv_anim_t ah;
    lv_anim_init(&ah);
    lv_anim_set_var(&ah, hint);
    lv_anim_set_exec_cb(&ah, anim_opa_cb);
    lv_anim_set_values(&ah, 40, 160);
    lv_anim_set_time(&ah, 2800);
    lv_anim_set_playback_time(&ah, 2800);
    lv_anim_set_delay(&ah, 1200);
    lv_anim_set_repeat_count(&ah, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&ah, lv_anim_path_ease_in_out);
    lv_anim_start(&ah);
}

// ─── Pre-production view ──────────────────────────────────────────────────────
static void on_start_btn(lv_event_t *) { g_start_pressed = true; }

static void create_preprod(const Config &cfg) {
    g_preprod = cont(lv_screen_active());
    lv_obj_set_pos(g_preprod, 0, 0);
    lv_obj_set_size(g_preprod, W, H);
    lv_obj_set_style_bg_color(g_preprod, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(g_preprod, LV_OPA_COVER, 0);
    lv_obj_add_flag(g_preprod, LV_OBJ_FLAG_HIDDEN);

    // Machine info (large, centered above button)
    lv_obj_t *mid = lbl(g_preprod, cfg.machine_id.c_str(), &lv_font_montserrat_32, C_TEXT);
    lv_obj_align(mid, LV_ALIGN_CENTER, 0, -130);

    lv_obj_t *lid = lbl(g_preprod, cfg.line_id.c_str(), &lv_font_montserrat_20, C_MUTED);
    lv_obj_align(lid, LV_ALIGN_CENTER, 0, -82);

    // Divider
    lv_obj_t *div = lv_obj_create(g_preprod);
    lv_obj_set_size(div, 400, 1);
    lv_obj_align(div, LV_ALIGN_CENTER, 0, -50);
    lv_obj_set_style_bg_color(div, lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(div, 0, 0);
    lv_obj_set_style_radius(div, 0, 0);

    // URETIME BASLA button
    lv_obj_t *btn = lv_obj_create(g_preprod);
    lv_obj_set_size(btn, 480, 110);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x0a2010), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(C_GREEN), 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, on_start_btn, LV_EVENT_CLICKED, nullptr);

    // Button hover effect (pressed state)
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x163320), LV_STATE_PRESSED);

    lv_obj_t *btn_lbl = lbl(btn, "URETIME BASLA", &lv_font_montserrat_32, C_GREEN);
    lv_obj_align(btn_lbl, LV_ALIGN_CENTER, 0, 0);

    // Button pulse animation
    lv_anim_t ab;
    lv_anim_init(&ab);
    lv_anim_set_var(&ab, btn);
    lv_anim_set_exec_cb(&ab, anim_opa_cb);
    lv_anim_set_values(&ab, 200, 255);
    lv_anim_set_time(&ab, 1200);
    lv_anim_set_playback_time(&ab, 1200);
    lv_anim_set_repeat_count(&ab, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&ab, lv_anim_path_ease_in_out);
    lv_anim_start(&ab);

    // Instruction hint
    lv_obj_t *hint = lbl(g_preprod, "uretime baslamak icin butona basin", &lv_font_montserrat_14, C_MUTED);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 120);
}

// ─── Mode switch ──────────────────────────────────────────────────────────────
static void hide_all() {
    if (g_header)      lv_obj_add_flag(g_header,      LV_OBJ_FLAG_HIDDEN);
    if (g_screensaver) lv_obj_add_flag(g_screensaver, LV_OBJ_FLAG_HIDDEN);
    if (g_preprod)     lv_obj_add_flag(g_preprod,     LV_OBJ_FLAG_HIDDEN);
    if (g_detail)      lv_obj_add_flag(g_detail,      LV_OBJ_FLAG_HIDDEN);
    if (g_idle)        lv_obj_add_flag(g_idle,        LV_OBJ_FLAG_HIDDEN);
}

static void show_header() {
    if (g_header) lv_obj_clear_flag(g_header, LV_OBJ_FLAG_HIDDEN);
}

static void switch_to_screensaver() {
    // Notify Pico to stop sending cycle data
    if (g_cmd_queue) g_cmd_queue->push("STOP\n");
    hide_all();
    lv_obj_clear_flag(g_screensaver, LV_OBJ_FLAG_HIDDEN);
    g_mode = Mode::SCREENSAVER;
}

static void switch_to_preprod() {
    hide_all();
    lv_obj_clear_flag(g_preprod, LV_OBJ_FLAG_HIDDEN);
    g_mode = Mode::PRE_PROD;
}

static void switch_to_idle() {
    hide_all();
    show_header();
    lv_obj_clear_flag(g_idle, LV_OBJ_FLAG_HIDDEN);
    g_mode = Mode::IDLE;
}

static void switch_to_detail() {
    hide_all();
    show_header();
    lv_obj_clear_flag(g_detail, LV_OBJ_FLAG_HIDDEN);
    g_mode = Mode::DETAIL;
}

// ─── Badge / header helpers ────────────────────────────────────────────────────
static void set_badge(const char *text, uint32_t color) {
    lv_label_set_text(g_badge_lbl, text);
    lv_obj_set_style_text_color(g_badge_lbl, lv_color_hex(color), 0);
    lv_obj_set_style_border_color(g_badge_box, lv_color_hex(color), 0);
    // bg: dark tint of color
    lv_obj_set_style_bg_color(g_badge_box,
        lv_color_mix(lv_color_hex(color), lv_color_hex(C_PANEL), 40), 0);
}

// ─── Update ───────────────────────────────────────────────────────────────────
struct Snap {
    uint32_t last_cycle;
    uint64_t cycle_count, anomaly_count;
    double   mean, sigma, ucl, lcl, ucl_mr, mr_bar;
    bool     warming_up, limits_locked;
};

static void update_badge(const Snap &s) {
    if (s.warming_up) {
        set_badge("ISINIYOR", C_AMBER);
    } else if (g_was_anomaly) {
        set_badge("ANOMALI",  C_RED);
    } else {
        set_badge("KONTROLDE", C_GREEN);
    }
}

static void update_clock() {
    time_t t = time(nullptr);
    struct tm *tm_info = localtime(&t);
    char buf[12];
    strftime(buf, sizeof(buf), "%H:%M:%S", tm_info);
    lv_label_set_text(g_clock_lbl, buf);
}

static void update_detail(const Snap &s, const Config &cfg) {
    // Calibration snapshot
    if (s.limits_locked && !g_calib_saved) {
        g_calib_mean  = s.mean;
        g_calib_sigma = s.sigma;
        g_calib_saved = true;
    }

    // Son Cycle card
    if (s.cycle_count > 0) {
        lv_label_set_text(g_cycle_lbl, fmt_sec(s.last_cycle).c_str());
        if (g_calib_saved) {
            double delta_s = (static_cast<double>(s.last_cycle) - s.mean) / 1e6;
            char db[32];
            std::snprintf(db, sizeof(db), "%+.2f s", delta_s);
            lv_label_set_text(g_delta_lbl, db);
            // Positive delta = slower than mean (bad → red)
            // Negative delta = faster than mean (good → green)
            uint32_t dc = (delta_s > 0.0) ? C_RED : C_GREEN;
            lv_obj_set_style_text_color(g_delta_lbl, lv_color_hex(dc), 0);
        }
    }

    // Ortalama card (live)
    lv_label_set_text(g_mean_live_lbl, fmt_dsec(s.mean).c_str());
    char sb[32];
    std::snprintf(sb, sizeof(sb), "s = %.3f s", s.sigma / 1e6);
    lv_label_set_text(g_sigma_live_lbl, sb);

    // Kontrol Limitleri (calibration — update only once)
    if (s.limits_locked && g_calib_saved) {
        lv_label_set_text(g_ucl_lbl,    fmt_dsec(s.ucl).c_str());
        lv_label_set_text(g_calib_m_lbl,fmt_dsec(g_calib_mean).c_str());
        if (s.lcl <= 0.0)
            lv_label_set_text(g_lcl_lbl, "N/A");
        else
            lv_label_set_text(g_lcl_lbl, fmt_dsec(s.lcl).c_str());
        lv_label_set_text(g_calib_s_lbl,fmt_dsec(g_calib_sigma).c_str());
    }

    // Kalibrasyon bar
    int32_t bar_v = (int32_t)std::min(s.cycle_count, (uint64_t)cfg.min_samples);
    lv_bar_set_value(g_kalib_bar, bar_v, LV_ANIM_OFF);
    if (s.limits_locked) {
        lv_label_set_text(g_kalib_status, "tamamlandi");
        lv_obj_set_style_text_color(g_kalib_status, lv_color_hex(C_GREEN), 0);
    } else {
        char kb[32];
        std::snprintf(kb, sizeof(kb), "ISINIYOR  %llu/%zu",
                      (unsigned long long)s.cycle_count, cfg.min_samples);
        lv_label_set_text(g_kalib_status, kb);
        lv_obj_set_style_text_color(g_kalib_status, lv_color_hex(C_AMBER), 0);
    }

    // I chart ort label
    {
        char ob[64];
        std::snprintf(ob, sizeof(ob), "ort: %.3f s  |  s: %.3f s",
                      s.mean / 1e6, s.sigma / 1e6);
        lv_label_set_text(g_i_ort_lbl, ob);
    }

    // Bottom stats
    lv_label_set_text(g_prod_lbl,   std::to_string(s.cycle_count).c_str());
    lv_label_set_text(g_anom_d_lbl, std::to_string(s.anomaly_count).c_str());

    // Chart update (on new cycle)
    if (s.cycle_count > g_last_count && s.cycle_count > 0) {
        g_last_count = s.cycle_count;

        double mr = g_has_prev
            ? std::fabs(static_cast<double>(s.last_cycle) - static_cast<double>(g_prev_cycle))
            : 0.0;
        g_prev_cycle = s.last_cycle;
        g_has_prev   = true;

        // I chart (ms scale)
        lv_chart_set_next_value(g_i_chart, g_i_data, (int32_t)(s.last_cycle / 1000));
        // MR chart (ms scale)
        lv_chart_set_next_value(g_mr_chart, g_mr_data, (int32_t)(mr / 1000));

        // ── I Chart: data-driven Y range ──────────────────────────────────
        {
            auto *ipts = lv_chart_get_y_array(g_i_chart, g_i_data);
            int32_t dmin = INT32_MAX, dmax = INT32_MIN;
            for (int i = 0; i < CHART_POINTS; ++i) {
                auto v = ipts[i];
                if (v != LV_CHART_POINT_NONE && v > 0) {
                    dmin = std::min(dmin, (int32_t)v);
                    dmax = std::max(dmax, (int32_t)v);
                }
            }
            if (dmax > dmin && dmax > 0) {
                int32_t margin = std::max(30, (dmax - dmin) / 3);
                int32_t y_min  = std::max(0, dmin - margin);
                int32_t y_max  = dmax + margin;
                lv_chart_set_range(g_i_chart, LV_CHART_AXIS_PRIMARY_Y, y_min, y_max);

                // Draw UCL/LCL once, clamped to visible range
                if (!g_limits_drawn && s.limits_locked) {
                    int32_t ucl_ms = (int32_t)(s.ucl / 1000);
                    int32_t lcl_ms = (int32_t)(s.lcl / 1000);
                    series_const(g_i_chart, g_i_ucl, std::min(ucl_ms, y_max));
                    if (s.lcl > 0)
                        series_const(g_i_chart, g_i_lcl, std::max(lcl_ms, y_min));
                    else {
                        // LCL negative — hide reference line
                        auto *pl = lv_chart_get_y_array(g_i_chart, g_i_lcl);
                        for (int i = 0; i < CHART_POINTS; ++i) pl[i] = LV_CHART_POINT_NONE;
                    }
                    g_limits_drawn = true;
                }
            }
        }

        // ── MR Chart: data-driven Y range ─────────────────────────────────
        {
            auto *mrpts = lv_chart_get_y_array(g_mr_chart, g_mr_data);
            int32_t mr_max = 0;
            for (int i = 0; i < CHART_POINTS; ++i)
                if (mrpts[i] != LV_CHART_POINT_NONE && mrpts[i] > mr_max)
                    mr_max = mrpts[i];

            if (mr_max > 0) {
                int32_t ucl_mr_ms = s.limits_locked ? (int32_t)(s.ucl_mr / 1000) : 0;
                int32_t y_top = std::max({mr_max + mr_max/5, ucl_mr_ms + 20, 50});
                lv_chart_set_range(g_mr_chart, LV_CHART_AXIS_PRIMARY_Y, 0, y_top);

                if (s.limits_locked) {
                    // Redraw MR UCL line when data range changes significantly
                    series_const(g_mr_chart, g_mr_ucl, ucl_mr_ms);

                    char mb[48];
                    std::snprintf(mb, sizeof(mb), "UCL_MR: %.3f s", s.ucl_mr / 1e6);
                    lv_label_set_text(g_mr_ucl_lbl, mb);
                }
            }
        }

        lv_chart_refresh(g_i_chart);
        lv_chart_refresh(g_mr_chart);
    }
}

static void update_idle(const Snap &s) {
    // Big status box
    const char *status_text = "DURAKLATILDI";
    const char *sub_text    = "35 sn yeni veri gelmedi";
    uint32_t    box_color   = 0x1a1500;
    uint32_t    text_color  = C_AMBER;
    uint32_t    border_color= C_AMBER;

    if (s.warming_up) {
        status_text  = "ISINIYOR";
        sub_text     = "KALIBRASYON DEVAM EDIYOR";
        box_color    = 0x1a1500;
        text_color   = C_AMBER;
        border_color = C_AMBER;
    } else if (g_was_anomaly) {
        status_text  = "ANOMALI";
        sub_text     = "SON CYCLE KONTROL SINIRI DISI";
        box_color    = 0x200d0d;
        text_color   = C_RED;
        border_color = C_RED;
    } else {
        status_text  = "KONTROLDE";
        sub_text     = "TUM PARAMETRELER NORMAL ARALIKTA";
        box_color    = 0x0d1f0f;
        text_color   = C_GREEN;
        border_color = C_GREEN;
    }

    lv_obj_set_style_bg_color(g_idle_box, lv_color_hex(box_color), 0);
    lv_obj_set_style_border_color(g_idle_box, lv_color_hex(border_color), 0);
    lv_label_set_text(g_idle_status, status_text);
    lv_obj_set_style_text_color(g_idle_status, lv_color_hex(text_color), 0);
    lv_obj_set_style_text_color(g_idle_sub, lv_color_hex(text_color & 0x507050), 0);
    lv_label_set_text(g_idle_sub, sub_text);

    // 3 cards
    lv_obj_set_style_text_color(g_idle_cycle, lv_color_hex(text_color), 0);
    lv_obj_set_style_text_color(g_idle_anom,  lv_color_hex(text_color), 0);
    lv_obj_set_style_text_color(g_idle_prod,  lv_color_hex(text_color), 0);

    if (s.cycle_count > 0)
        lv_label_set_text(g_idle_cycle, fmt_sec(s.last_cycle).c_str());
    lv_label_set_text(g_idle_anom, std::to_string(s.anomaly_count).c_str());
    lv_label_set_text(g_idle_prod, std::to_string(s.cycle_count).c_str());
}

// ─── Platform init ────────────────────────────────────────────────────────────
static void platform_init() {
#ifdef KAIROS_SDL2
    lv_sdl_window_create(W, H);
    lv_sdl_mouse_create();
    lv_sdl_mousewheel_create();
#endif
#ifdef KAIROS_FBDEV
    lv_display_t *d = lv_linux_fbdev_create();
    lv_linux_fbdev_set_file(d, "/dev/fb0");
#endif
}

// ─── Thread entry ─────────────────────────────────────────────────────────────
void display_thread_func(const Config&      cfg,
                         SharedState&       state,
                         CommandQueue&      cmd_queue,
                         Logger&            logger,
                         std::atomic<bool>& running) {
    g_cmd_queue = &cmd_queue;

    lv_init();
    platform_init();

    // Register press callback on every input device (event-driven, no polling)
    for (lv_indev_t *iv = lv_indev_get_next(nullptr); iv; iv = lv_indev_get_next(iv))
        lv_indev_add_event_cb(iv, on_any_press, LV_EVENT_PRESSED, nullptr);

    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);

    // Screensaver and pre-prod are full-screen (no header)
    create_screensaver(cfg);
    create_preprod(cfg);
    // Detail/idle share the persistent header
    create_header(cfg);
    create_detail(cfg);
    create_idle();
    // Start in screensaver; header and other views hidden
    lv_obj_add_flag(g_header, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_detail, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_idle,   LV_OBJ_FLAG_HIDDEN);

    logger.info("Display thread started.");

    // Dedicated tick thread (1ms resolution)
    std::atomic<bool> tick_run{true};
    std::thread tick_t([&]() {
        while (tick_run.load()) {
            lv_tick_inc(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    auto last_activity = std::chrono::steady_clock::now();

    while (running.load()) {
        auto now = std::chrono::steady_clock::now();

        // Read SharedState
        Snap s{};
        {
            std::shared_lock lock(state.mtx);
            s.last_cycle    = state.last_cycle;
            s.cycle_count   = state.cycle_count;
            s.anomaly_count = state.anomaly_count;
            s.mean          = state.mean;
            s.sigma         = state.sigma;
            s.ucl           = state.ucl;
            s.lcl           = state.lcl;
            s.ucl_mr        = state.ucl_mr;
            s.mr_bar        = state.mr_bar;
            s.warming_up    = state.warming_up;
            s.limits_locked = state.limits_locked;
        }

        // Track anomaly state
        if (s.limits_locked && s.cycle_count > 0) {
            g_was_anomaly = (static_cast<double>(s.last_cycle) > s.ucl) ||
                            (s.lcl > 0 && static_cast<double>(s.last_cycle) < s.lcl);
        }

        // ── State machine ────────────────────────────────────────────────────

        // Any touch: reset idle timer, handle state transitions
        if (g_any_press) {
            g_any_press   = false;
            last_activity = now;
            if (g_mode == Mode::SCREENSAVER) {
                switch_to_preprod();
                logger.info("Display: touch — screensaver → pre-production");
            } else if (g_mode == Mode::IDLE) {
                switch_to_detail();
                logger.info("Display: touch — idle → detail");
            }
        }

        // "URETIME BASLA" button pressed
        if (g_start_pressed && g_mode == Mode::PRE_PROD) {
            g_start_pressed = false;
            cmd_queue.push("START\n");
            last_activity = now;
            switch_to_detail();
            logger.info("Display: production started — START sent to Pico");
        }

        // "URETIM BITTI" button pressed (header button, visible in DETAIL/IDLE)
        if (g_stop_pressed && (g_mode == Mode::DETAIL || g_mode == Mode::IDLE)) {
            g_stop_pressed = false;
            // Reset display state for next production run
            g_calib_mean  = 0.0;
            g_calib_sigma = 0.0;
            g_calib_saved = false;
            g_limits_drawn= false;
            g_last_count  = 0;
            g_prev_cycle  = 0;
            g_has_prev    = false;
            g_was_anomaly = false;
            switch_to_screensaver();  // also sends STOP\n
            logger.info("Display: production ended — STOP sent to Pico");
        }

        // Idle timeout (only in DETAIL mode)
        auto idle_s = std::chrono::duration_cast<std::chrono::seconds>(
            now - last_activity).count();
        if (g_mode == Mode::DETAIL && idle_s >= cfg.idle_timeout_s) {
            switch_to_idle();
            logger.info("Display: idle timeout → idle view");
        }

        // ── View update ──────────────────────────────────────────────────────

        // Header only visible in DETAIL / IDLE
        bool show_header = (g_mode == Mode::DETAIL || g_mode == Mode::IDLE);
        if (show_header) {
            update_clock();
            update_badge(s);
        }

        if      (g_mode == Mode::DETAIL) update_detail(s, cfg);
        else if (g_mode == Mode::IDLE)   update_idle(s);
        // SCREENSAVER and PRE_PROD are animation-driven — no frame update needed

        lv_timer_handler();
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    tick_run = false;
    tick_t.join();

    logger.info("Display thread stopped.");
}
