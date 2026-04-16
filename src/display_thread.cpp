#include "display_thread.hpp"

#include <lvgl.h>
#include <string>
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
static constexpr int SCREEN_W     = 1024;
static constexpr int SCREEN_H     = 600;
static constexpr int CHART_POINTS = 50;
static constexpr int REFRESH_MS   = 33;  // ~30 fps

// Scale: chart shows milliseconds (divide us by 1000)
static constexpr int32_t SCALE = 1000;

// ─── Layout (all y positions absolute) ────────────────────────────────────────
// Header:     y=0,   h=54
// Top panels: y=58,  h=148
// I chart:    y=210, h=160
// MR chart:   y=374, h=120
// Bottom bar: y=498, h=98

// ─── Colors ───────────────────────────────────────────────────────────────────
static constexpr uint32_t CLR_BG      = 0x0D1117;
static constexpr uint32_t CLR_PANEL   = 0x161B22;
static constexpr uint32_t CLR_BORDER  = 0x30363D;
static constexpr uint32_t CLR_TEXT    = 0xE6EDF3;
static constexpr uint32_t CLR_MUTED   = 0x8B949E;
static constexpr uint32_t CLR_NORMAL  = 0x3FB950;
static constexpr uint32_t CLR_ANOMALY = 0xF85149;
static constexpr uint32_t CLR_WARN    = 0xD29922;
static constexpr uint32_t CLR_BLUE    = 0x58A6FF;
static constexpr uint32_t CLR_LIMIT   = 0xF85149;

// ─── Widget handles ───────────────────────────────────────────────────────────
static lv_obj_t *g_status_dot   = nullptr;
static lv_obj_t *g_status_lbl   = nullptr;
static lv_obj_t *g_cycle_lbl    = nullptr;
static lv_obj_t *g_ucl_lbl      = nullptr;
static lv_obj_t *g_mean_lbl     = nullptr;
static lv_obj_t *g_lcl_lbl      = nullptr;
static lv_obj_t *g_sigma_lbl    = nullptr;
static lv_obj_t *g_prod_lbl     = nullptr;
static lv_obj_t *g_anom_lbl     = nullptr;
static lv_obj_t *g_state_lbl    = nullptr;
static lv_obj_t *g_i_chart      = nullptr;
static lv_obj_t *g_mr_chart     = nullptr;
static lv_obj_t *g_mr_ucl_lbl   = nullptr;

static lv_chart_series_t *g_i_data  = nullptr;
static lv_chart_series_t *g_i_ucl   = nullptr;
static lv_chart_series_t *g_i_lcl   = nullptr;
static lv_chart_series_t *g_mr_data = nullptr;
static lv_chart_series_t *g_mr_ucl  = nullptr;

// ─── Helpers ──────────────────────────────────────────────────────────────────
// Format microseconds as e.g. "1.002.847 us" (ASCII only)
static std::string fmt_us(uint32_t us) {
    char buf[32];
    if (us >= 1000000u) {
        std::snprintf(buf, sizeof(buf), "%u.%03u.%03u us",
                      us / 1000000u, (us % 1000000u) / 1000u, us % 1000u);
    } else if (us >= 1000u) {
        std::snprintf(buf, sizeof(buf), "%u.%03u us",
                      us / 1000u, us % 1000u);
    } else {
        std::snprintf(buf, sizeof(buf), "%u us", us);
    }
    return buf;
}

static std::string fmt_double_us(double d) {
    uint32_t v = (d < 0.0) ? 0u : static_cast<uint32_t>(d);
    return fmt_us(v);
}

static void panel_style(lv_obj_t *o) {
    lv_obj_set_style_bg_color(o, lv_color_hex(CLR_PANEL), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(CLR_BORDER), 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_radius(o, 6, 0);
    lv_obj_set_style_pad_all(o, 10, 0);
}

static lv_obj_t * kv_row(lv_obj_t *parent, const char *key, uint32_t key_clr,
                          int y, lv_obj_t **val_out) {
    lv_obj_t *k = lv_label_create(parent);
    lv_label_set_text(k, key);
    lv_obj_set_style_text_font(k, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(k, lv_color_hex(key_clr), 0);
    lv_obj_align(k, LV_ALIGN_TOP_LEFT, 0, y);

    lv_obj_t *v = lv_label_create(parent);
    lv_label_set_text(v, "---");
    lv_obj_set_style_text_font(v, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(v, lv_color_hex(CLR_TEXT), 0);
    lv_obj_align(v, LV_ALIGN_TOP_RIGHT, 0, y);
    *val_out = v;
    return k;
}

// Fill all points of a chart series with a constant value
static void series_fill_const(lv_obj_t *chart, lv_chart_series_t *ser, int32_t val) {
    lv_value_precise_t *pts = lv_chart_get_y_array(chart, ser);
    for (int i = 0; i < CHART_POINTS; ++i) pts[i] = static_cast<lv_value_precise_t>(val);
    lv_chart_refresh(chart);
}

// ─── UI creation ──────────────────────────────────────────────────────────────
static void create_ui() {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(CLR_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // ── Header ──────────────────────────────────────────────────────────────
    lv_obj_t *hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, SCREEN_W, 54);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(CLR_PANEL), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(hdr, lv_color_hex(CLR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_border_width(hdr, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_hor(hdr, 16, 0);
    lv_obj_set_style_pad_ver(hdr, 0, 0);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, "KAIROS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(CLR_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 0, 0);

    g_status_dot = lv_obj_create(hdr);
    lv_obj_set_size(g_status_dot, 12, 12);
    lv_obj_set_style_radius(g_status_dot, 6, 0);
    lv_obj_set_style_border_width(g_status_dot, 0, 0);
    lv_obj_set_style_bg_color(g_status_dot, lv_color_hex(CLR_WARN), 0);
    lv_obj_align(g_status_dot, LV_ALIGN_RIGHT_MID, -105, 0);

    g_status_lbl = lv_label_create(hdr);
    lv_label_set_text(g_status_lbl, "ISINIYOR");
    lv_obj_set_style_text_font(g_status_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_status_lbl, lv_color_hex(CLR_WARN), 0);
    lv_obj_align(g_status_lbl, LV_ALIGN_RIGHT_MID, -8, 0);

    // ── Top panels ──────────────────────────────────────────────────────────
    // Left: current cycle
    lv_obj_t *left = lv_obj_create(scr);
    lv_obj_set_pos(left, 4, 58);
    lv_obj_set_size(left, 370, 148);
    panel_style(left);

    lv_obj_t *ct = lv_label_create(left);
    lv_label_set_text(ct, "SON CYCLE");
    lv_obj_set_style_text_font(ct, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ct, lv_color_hex(CLR_MUTED), 0);
    lv_obj_align(ct, LV_ALIGN_TOP_LEFT, 0, 0);

    g_cycle_lbl = lv_label_create(left);
    lv_label_set_text(g_cycle_lbl, "---");
    lv_obj_set_style_text_font(g_cycle_lbl, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(g_cycle_lbl, lv_color_hex(CLR_TEXT), 0);
    lv_obj_align(g_cycle_lbl, LV_ALIGN_CENTER, 0, 8);

    // Right: control limits (I chart)
    lv_obj_t *right = lv_obj_create(scr);
    lv_obj_set_pos(right, 378, 58);
    lv_obj_set_size(right, 642, 148);
    panel_style(right);

    lv_obj_t *rt = lv_label_create(right);
    lv_label_set_text(rt, "I CHART - KONTROL LIMITLERI");
    lv_obj_set_style_text_font(rt, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(rt, lv_color_hex(CLR_MUTED), 0);
    lv_obj_align(rt, LV_ALIGN_TOP_LEFT, 0, 0);

    kv_row(right, "UCL",   CLR_LIMIT, 22, &g_ucl_lbl);
    kv_row(right, "X-bar", CLR_BLUE,  52, &g_mean_lbl);
    kv_row(right, "LCL",   CLR_LIMIT, 82, &g_lcl_lbl);
    kv_row(right, "Sigma", CLR_MUTED, 112, &g_sigma_lbl);

    // ── I Chart ─────────────────────────────────────────────────────────────
    lv_obj_t *i_panel = lv_obj_create(scr);
    lv_obj_set_pos(i_panel, 4, 210);
    lv_obj_set_size(i_panel, 1016, 160);
    panel_style(i_panel);
    lv_obj_set_style_pad_all(i_panel, 6, 0);

    lv_obj_t *i_title = lv_label_create(i_panel);
    lv_label_set_text(i_title, "I Chart (Bireysel Degerler)");
    lv_obj_set_style_text_font(i_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(i_title, lv_color_hex(CLR_MUTED), 0);
    lv_obj_align(i_title, LV_ALIGN_TOP_LEFT, 0, 0);

    g_i_chart = lv_chart_create(i_panel);
    lv_obj_set_size(g_i_chart, 1000, 132);
    lv_obj_align(g_i_chart, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_chart_set_type(g_i_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(g_i_chart, CHART_POINTS);
    lv_chart_set_range(g_i_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 2000);
    lv_obj_set_style_bg_color(g_i_chart, lv_color_hex(CLR_PANEL), 0);
    lv_obj_set_style_border_width(g_i_chart, 0, 0);
    lv_obj_set_style_size(g_i_chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(g_i_chart, 2, LV_PART_ITEMS);

    g_i_data = lv_chart_add_series(g_i_chart, lv_color_hex(CLR_BLUE),  LV_CHART_AXIS_PRIMARY_Y);
    g_i_ucl  = lv_chart_add_series(g_i_chart, lv_color_hex(CLR_LIMIT), LV_CHART_AXIS_PRIMARY_Y);
    g_i_lcl  = lv_chart_add_series(g_i_chart, lv_color_hex(CLR_LIMIT), LV_CHART_AXIS_PRIMARY_Y);

    // Init UCL/LCL as hidden
    lv_value_precise_t *p = lv_chart_get_y_array(g_i_chart, g_i_ucl);
    lv_value_precise_t *q = lv_chart_get_y_array(g_i_chart, g_i_lcl);
    for (int i = 0; i < CHART_POINTS; ++i) { p[i] = LV_CHART_POINT_NONE; q[i] = LV_CHART_POINT_NONE; }

    // ── MR Chart ────────────────────────────────────────────────────────────
    lv_obj_t *mr_panel = lv_obj_create(scr);
    lv_obj_set_pos(mr_panel, 4, 374);
    lv_obj_set_size(mr_panel, 1016, 120);
    panel_style(mr_panel);
    lv_obj_set_style_pad_all(mr_panel, 6, 0);

    lv_obj_t *mr_title = lv_label_create(mr_panel);
    lv_label_set_text(mr_title, "MR Chart (Hareketli Aralik)");
    lv_obj_set_style_text_font(mr_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(mr_title, lv_color_hex(CLR_MUTED), 0);
    lv_obj_align(mr_title, LV_ALIGN_TOP_LEFT, 0, 0);

    g_mr_ucl_lbl = lv_label_create(mr_panel);
    lv_label_set_text(g_mr_ucl_lbl, "UCL_MR: ---");
    lv_obj_set_style_text_font(g_mr_ucl_lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_mr_ucl_lbl, lv_color_hex(CLR_LIMIT), 0);
    lv_obj_align(g_mr_ucl_lbl, LV_ALIGN_TOP_RIGHT, 0, 0);

    g_mr_chart = lv_chart_create(mr_panel);
    lv_obj_set_size(g_mr_chart, 1000, 92);
    lv_obj_align(g_mr_chart, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_chart_set_type(g_mr_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(g_mr_chart, CHART_POINTS);
    lv_chart_set_range(g_mr_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 500);
    lv_obj_set_style_bg_color(g_mr_chart, lv_color_hex(CLR_PANEL), 0);
    lv_obj_set_style_border_width(g_mr_chart, 0, 0);
    lv_obj_set_style_size(g_mr_chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(g_mr_chart, 2, LV_PART_ITEMS);

    g_mr_data = lv_chart_add_series(g_mr_chart, lv_color_hex(0xE3B341), LV_CHART_AXIS_PRIMARY_Y);  // amber
    g_mr_ucl  = lv_chart_add_series(g_mr_chart, lv_color_hex(CLR_LIMIT), LV_CHART_AXIS_PRIMARY_Y);

    lv_value_precise_t *mp = lv_chart_get_y_array(g_mr_chart, g_mr_ucl);
    for (int i = 0; i < CHART_POINTS; ++i) mp[i] = LV_CHART_POINT_NONE;

    // ── Bottom bar ──────────────────────────────────────────────────────────
    lv_obj_t *bot = lv_obj_create(scr);
    lv_obj_set_pos(bot, 4, 498);
    lv_obj_set_size(bot, 1016, 98);
    panel_style(bot);

    // Helper: bottom stat cell
    auto make_cell = [&](int x, int w, const char *key, lv_obj_t **val_lbl) {
        lv_obj_t *cell = lv_obj_create(bot);
        lv_obj_set_size(cell, w, 74);
        lv_obj_align(cell, LV_ALIGN_LEFT_MID, x, 0);
        lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(cell, 0, 0);
        lv_obj_set_style_pad_all(cell, 0, 0);

        lv_obj_t *k = lv_label_create(cell);
        lv_label_set_text(k, key);
        lv_obj_set_style_text_font(k, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(k, lv_color_hex(CLR_MUTED), 0);
        lv_obj_align(k, LV_ALIGN_TOP_LEFT, 0, 0);

        lv_obj_t *v = lv_label_create(cell);
        lv_label_set_text(v, "0");
        lv_obj_set_style_text_font(v, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(v, lv_color_hex(CLR_TEXT), 0);
        lv_obj_align(v, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        *val_lbl = v;
    };

    make_cell(0,   300, "Uretim",  &g_prod_lbl);
    make_cell(310, 300, "Anomali", &g_anom_lbl);

    // Status cell
    lv_obj_t *sc = lv_obj_create(bot);
    lv_obj_set_size(sc, 380, 74);
    lv_obj_align(sc, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_opa(sc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sc, 0, 0);
    lv_obj_set_style_pad_all(sc, 0, 0);

    lv_obj_t *sk = lv_label_create(sc);
    lv_label_set_text(sk, "Durum");
    lv_obj_set_style_text_font(sk, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sk, lv_color_hex(CLR_MUTED), 0);
    lv_obj_align(sk, LV_ALIGN_TOP_LEFT, 0, 0);

    g_state_lbl = lv_label_create(sc);
    lv_label_set_text(g_state_lbl, "BEKLENIYOR");
    lv_obj_set_style_text_font(g_state_lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_state_lbl, lv_color_hex(CLR_WARN), 0);
    lv_obj_align(g_state_lbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

// ─── UI update ────────────────────────────────────────────────────────────────
struct Snap {
    uint32_t last_cycle;
    uint64_t cycle_count;
    uint64_t anomaly_count;
    double   mean, sigma, ucl, lcl, ucl_mr, mr_bar;
    bool     warming_up, limits_locked;
};

static bool     g_limits_drawn   = false;
static uint64_t g_last_count     = 0;
static uint32_t g_prev_cycle     = 0;
static bool     g_has_prev       = false;

static void update_ui(const Snap &s) {
    // ── Cycle label ────────────────────────────────────────────────────────
    if (s.cycle_count > 0)
        lv_label_set_text(g_cycle_lbl, fmt_us(s.last_cycle).c_str());

    // ── Status indicator ──────────────────────────────────────────────────
    if (s.warming_up) {
        lv_obj_set_style_bg_color(g_status_dot, lv_color_hex(CLR_WARN), 0);
        lv_obj_set_style_text_color(g_status_lbl, lv_color_hex(CLR_WARN), 0);
        lv_label_set_text(g_status_lbl, "ISINIYOR");
        lv_obj_set_style_text_color(g_state_lbl, lv_color_hex(CLR_WARN), 0);
        lv_label_set_text(g_state_lbl, "KALIBRASYON");
    } else {
        bool anomaly = s.limits_locked && s.cycle_count > 0 &&
                       (static_cast<double>(s.last_cycle) > s.ucl ||
                        (s.lcl > 0.0 && static_cast<double>(s.last_cycle) < s.lcl));
        uint32_t dot_c  = anomaly ? CLR_ANOMALY : CLR_NORMAL;
        const char *st  = anomaly ? "ANOMALI"   : "NORMAL";
        const char *stb = anomaly ? "DISI"      : "KONTROLDE";
        lv_obj_set_style_bg_color(g_status_dot, lv_color_hex(dot_c), 0);
        lv_obj_set_style_text_color(g_status_lbl, lv_color_hex(dot_c), 0);
        lv_label_set_text(g_status_lbl, st);
        lv_obj_set_style_text_color(g_state_lbl, lv_color_hex(dot_c), 0);
        lv_label_set_text(g_state_lbl, stb);
    }

    // ── Control limits ────────────────────────────────────────────────────
    if (s.limits_locked) {
        lv_label_set_text(g_ucl_lbl,   fmt_double_us(s.ucl).c_str());
        lv_label_set_text(g_mean_lbl,  fmt_double_us(s.mean).c_str());
        lv_label_set_text(g_lcl_lbl,   fmt_double_us(s.lcl < 0 ? 0 : s.lcl).c_str());
        lv_label_set_text(g_sigma_lbl, fmt_double_us(s.sigma).c_str());

        char buf[48];
        std::snprintf(buf, sizeof(buf), "UCL_MR: %s", fmt_double_us(s.ucl_mr).c_str());
        lv_label_set_text(g_mr_ucl_lbl, buf);
    }

    // ── Counters ──────────────────────────────────────────────────────────
    lv_label_set_text(g_prod_lbl, (std::to_string(s.cycle_count) + " adet").c_str());
    lv_label_set_text(g_anom_lbl, std::to_string(s.anomaly_count).c_str());

    // ── Charts ────────────────────────────────────────────────────────────
    if (s.cycle_count > g_last_count && s.cycle_count > 0) {
        g_last_count = s.cycle_count;

        // Compute MR in display thread
        double mr = 0.0;
        if (g_has_prev) {
            mr = std::abs(static_cast<double>(s.last_cycle) -
                          static_cast<double>(g_prev_cycle));
        }
        g_prev_cycle = s.last_cycle;
        g_has_prev   = true;

        // I chart: push new value (ms scale)
        int32_t i_val = static_cast<int32_t>(s.last_cycle / SCALE);
        lv_chart_set_next_value(g_i_chart, g_i_data, i_val);

        // MR chart: push MR value (ms scale)
        int32_t mr_val = static_cast<int32_t>(mr / SCALE);
        lv_chart_set_next_value(g_mr_chart, g_mr_data, mr_val);

        if (s.limits_locked) {
            // I chart Y range: [lcl-margin, ucl+margin]
            int32_t ucl_ms = static_cast<int32_t>(s.ucl / SCALE);
            int32_t lcl_ms = static_cast<int32_t>(s.lcl < 0 ? 0 : s.lcl / SCALE);
            int32_t margin = std::max(50, (ucl_ms - lcl_ms) / 4);
            lv_chart_set_range(g_i_chart, LV_CHART_AXIS_PRIMARY_Y,
                               lcl_ms - margin, ucl_ms + margin);

            // MR chart Y range: [0, UCL_MR * 1.3]
            int32_t ucl_mr_ms = static_cast<int32_t>(s.ucl_mr / SCALE);
            lv_chart_set_range(g_mr_chart, LV_CHART_AXIS_PRIMARY_Y,
                               0, std::max(50, ucl_mr_ms + ucl_mr_ms / 3));

            // Draw reference lines (once after calibration)
            if (!g_limits_drawn) {
                series_fill_const(g_i_chart,  g_i_ucl,  ucl_ms);
                series_fill_const(g_i_chart,  g_i_lcl,  lcl_ms);
                series_fill_const(g_mr_chart, g_mr_ucl, ucl_mr_ms);
                g_limits_drawn = true;
            }
        }

        lv_chart_refresh(g_i_chart);
        lv_chart_refresh(g_mr_chart);
    }
}

// ─── Platform init ────────────────────────────────────────────────────────────
static void platform_init() {
#ifdef KAIROS_SDL2
    lv_sdl_window_create(SCREEN_W, SCREEN_H);
    lv_sdl_mouse_create();
    lv_sdl_mousewheel_create();
#endif
#ifdef KAIROS_FBDEV
    lv_display_t *disp = lv_linux_fbdev_create();
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
        auto now = std::chrono::steady_clock::now();
        uint32_t ms = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - last_tick).count());
        if (ms > 0) { lv_tick_inc(ms); last_tick = now; }

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

        update_ui(s);
        lv_timer_handler();

        std::this_thread::sleep_for(std::chrono::milliseconds(REFRESH_MS));
    }

    logger.info("Display thread stopped.");
}
