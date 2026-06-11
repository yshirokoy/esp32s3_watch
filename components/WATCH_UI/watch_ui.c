#include "watch_ui.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#include "esp_timer.h"
#include "weather.h"
#include "wifi_mgr.h"
#include "game_2048.h"

#define WATCH_DESIGN_H      280
#define WATCH_PAGE_COUNT    4
#define WATCH_SAFE_X        16
#define WATCH_SAFE_TOP      18
#define WATCH_SAFE_BOTTOM   28

// --- Page 0: Watch face ---
static lv_obj_t *s_tileview;
static lv_obj_t *s_tiles[WATCH_PAGE_COUNT];
static lv_obj_t *s_page_dots[WATCH_PAGE_COUNT];

static lv_obj_t *s_time_label;
static lv_obj_t *s_second_label;
static lv_obj_t *s_date_label;
static lv_obj_t *s_battery_label;
static lv_obj_t *s_steps_chip_label;
static lv_obj_t *s_heart_chip_label;
static lv_obj_t *s_weather_chip_label;
static lv_obj_t *s_main_arc;

// --- Page 1: Weather detail ---
static lv_obj_t *s_sun_glow;
static lv_obj_t *s_sun_core;
static lv_obj_t *s_moon_base;
static lv_obj_t *s_moon_shadow;
static lv_obj_t *s_weather_temp_label;
static lv_obj_t *s_weather_desc_label;
static lv_obj_t *s_weather_humidity_label;
static lv_obj_t *s_weather_wind_label;
static lv_obj_t *s_weather_update_label;
static lv_obj_t *s_refresh_btn;

static lv_timer_t *s_update_timer;

// --- Timer (stopwatch) state ---
static lv_obj_t *s_timer_label;
static lv_obj_t *s_timer_btn1;
static lv_obj_t *s_timer_btn2;
static lv_obj_t *s_timer_btn1_label;
static lv_obj_t *s_timer_btn2_label;
static int64_t  s_timer_start_us = 0;   // when current run started
static int64_t  s_timer_elapsed_us = 0; // accumulated elapsed before pause
static bool     s_timer_running = false;
static lv_timer_t *s_timer_tick = NULL; // 50ms update timer

// --- Font helpers ---
static const lv_font_t *font_time(void)
{
#if LV_FONT_MONTSERRAT_48
    return &lv_font_montserrat_48;
#elif LV_FONT_MONTSERRAT_40
    return &lv_font_montserrat_40;
#elif LV_FONT_MONTSERRAT_32
    return &lv_font_montserrat_32;
#else
    return LV_FONT_DEFAULT;
#endif
}

static const lv_font_t *font_mid(void)
{
#if LV_FONT_MONTSERRAT_24
    return &lv_font_montserrat_24;
#elif LV_FONT_MONTSERRAT_18
    return &lv_font_montserrat_18;
#else
    return LV_FONT_DEFAULT;
#endif
}

static const lv_font_t *font_body(void)
{
#if LV_FONT_MONTSERRAT_18
    return &lv_font_montserrat_18;
#else
    return LV_FONT_DEFAULT;
#endif
}

static lv_coord_t ui_w(void)
{
    lv_coord_t w = lv_disp_get_hor_res(NULL);
    return w > 0 ? w : 240;
}

static lv_coord_t ui_h(void)
{
    lv_coord_t h = lv_disp_get_ver_res(NULL);
    if (h <= 0) {
        return WATCH_DESIGN_H;
    }
    return h > WATCH_DESIGN_H ? WATCH_DESIGN_H : h;
}

// --- Widget builders ---
static void set_plain_bg(lv_obj_t *obj, uint32_t color)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                             const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    return label;
}

static lv_obj_t *make_panel(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                            lv_coord_t w, lv_coord_t h, uint32_t bg)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

static void setup_arc(lv_obj_t *arc, lv_coord_t size, uint32_t track,
                      uint32_t indicator, lv_coord_t width)
{
    lv_obj_set_size(arc, size, size);
    lv_arc_set_rotation(arc, 135);
    lv_arc_set_bg_angles(arc, 0, 270);
    lv_arc_set_range(arc, 0, 100);
    lv_obj_set_style_arc_color(arc, lv_color_hex(track), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(indicator), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
}

static lv_obj_t *make_circle(lv_obj_t *parent, lv_coord_t size,
                              uint32_t color, lv_opa_t opa)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, size, size);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

// --- Day/Night toggle helpers ---
static void show_sun(bool show)
{
    if (s_sun_glow) {
        if (show) lv_obj_clear_flag(s_sun_glow, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(s_sun_glow, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_sun_core) {
        if (show) lv_obj_clear_flag(s_sun_core, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(s_sun_core, LV_OBJ_FLAG_HIDDEN);
    }
}

static void show_moon(bool show)
{
    if (s_moon_base) {
        if (show) lv_obj_clear_flag(s_moon_base, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(s_moon_base, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_moon_shadow) {
        if (show) lv_obj_clear_flag(s_moon_shadow, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(s_moon_shadow, LV_OBJ_FLAG_HIDDEN);
    }
}

// --- Refresh button handler (non-blocking) ---
static void on_refresh_click(lv_event_t *e)
{
    (void)e;
    weather_request_refresh();
    if (s_refresh_btn) {
        lv_obj_set_style_bg_color(s_refresh_btn, lv_color_hex(0x42A5F5), 0);
    }
}

// ==================== Timer helpers ====================
static void timer_update_label(void)
{
    if (!s_timer_label) return;
    int64_t total_us = s_timer_elapsed_us;
    if (s_timer_running) {
        total_us += esp_timer_get_time() - s_timer_start_us;
    }
    if (total_us < 0) total_us = 0;
    int cs = (int)((total_us / 10000) % 100);      // centiseconds
    int sec = (int)((total_us / 1000000) % 60);
    int min = (int)((total_us / 60000000) % 60);
    lv_label_set_text_fmt(s_timer_label, "%02d:%02d.%02d", min, sec, cs);
}

static void timer_tick_cb(lv_timer_t *t)
{
    (void)t;
    timer_update_label();
}

static void timer_start(void)
{
    if (s_timer_running) return;
    s_timer_start_us = esp_timer_get_time();
    s_timer_running = true;
    if (!s_timer_tick) {
        s_timer_tick = lv_timer_create(timer_tick_cb, 50, NULL);
    }
    // Update button labels
    if (s_timer_btn1_label) lv_label_set_text(s_timer_btn1_label, "Stop");
    if (s_timer_btn2_label) lv_label_set_text(s_timer_btn2_label, "Lap");
}

static void timer_stop(void)
{
    if (!s_timer_running) return;
    s_timer_elapsed_us += esp_timer_get_time() - s_timer_start_us;
    s_timer_running = false;
    if (s_timer_tick) {
        lv_timer_del(s_timer_tick);
        s_timer_tick = NULL;
    }
    timer_update_label();
    if (s_timer_btn1_label) lv_label_set_text(s_timer_btn1_label, "Start");
    if (s_timer_btn2_label) lv_label_set_text(s_timer_btn2_label, "Reset");
}

static void timer_reset(void)
{
    if (s_timer_running) {
        s_timer_running = false;
        if (s_timer_tick) {
            lv_timer_del(s_timer_tick);
            s_timer_tick = NULL;
        }
    }
    s_timer_elapsed_us = 0;
    timer_update_label();
    if (s_timer_btn1_label) lv_label_set_text(s_timer_btn1_label, "Start");
    if (s_timer_btn2_label) lv_label_set_text(s_timer_btn2_label, "Reset");
}

// Timer button 1: Start/Stop
static void timer_btn1_cb(lv_event_t *e)
{
    (void)e;
    if (s_timer_running) timer_stop();
    else                 timer_start();
}

// Timer button 2: Reset/Lap (Lap just restarts for simplicity)
static void timer_btn2_cb(lv_event_t *e)
{
    (void)e;
    timer_reset();
}

// ==================== Page 0: Watch face ====================
static void build_watch_face(lv_obj_t *tile)
{
    const lv_coord_t w = ui_w();
    const lv_coord_t h = ui_h();

    // Warm espresso background
    set_plain_bg(tile, 0x1C110A);

    s_date_label = make_label(tile, "MON  APR 27", LV_FONT_DEFAULT, 0xA89080);
    lv_obj_align(s_date_label, LV_ALIGN_TOP_LEFT, WATCH_SAFE_X, WATCH_SAFE_TOP);

    s_battery_label = make_label(tile, "86%", LV_FONT_DEFAULT, 0xC8A060);
    lv_obj_align(s_battery_label, LV_ALIGN_TOP_RIGHT, -WATCH_SAFE_X, WATCH_SAFE_TOP);

    s_main_arc = lv_arc_create(tile);
    setup_arc(s_main_arc, 190, 0x2E2015, 0xE8954C, 6);
    lv_obj_align(s_main_arc, LV_ALIGN_TOP_MID, 0, 42);

    s_time_label = make_label(tile, "14:36", font_time(), 0xFDF0E0);
    lv_obj_align(s_time_label, LV_ALIGN_TOP_MID, 0, 74);

    s_second_label = make_label(tile, "08", font_mid(), 0xE8954C);
    lv_obj_align(s_second_label, LV_ALIGN_TOP_MID, 0, 130);

    lv_obj_t *hint = make_label(tile, "BT ON  |  1.69 WATCH", LV_FONT_DEFAULT, 0x7A6048);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 160);

    const lv_coord_t gap = 8;
    const lv_coord_t card_w = (w - WATCH_SAFE_X * 2 - gap * 2) / 3;
    const lv_coord_t card_y = h - WATCH_SAFE_BOTTOM - 52;

    lv_obj_t *steps = make_panel(tile, WATCH_SAFE_X, card_y, card_w, 44, 0x291C10);
    s_steps_chip_label = make_label(steps, "7842", font_body(), 0xFDF0E0);
    lv_obj_align(s_steps_chip_label, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_t *steps_cap = make_label(steps, "STEPS", LV_FONT_DEFAULT, 0xA89080);
    lv_obj_align(steps_cap, LV_ALIGN_BOTTOM_MID, 0, -5);

    lv_obj_t *heart = make_panel(tile, WATCH_SAFE_X + card_w + gap, card_y, card_w, 44, 0x291C10);
    s_heart_chip_label = make_label(heart, "72", font_body(), 0xF08070);
    lv_obj_align(s_heart_chip_label, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_t *heart_cap = make_label(heart, "BPM", LV_FONT_DEFAULT, 0xA89080);
    lv_obj_align(heart_cap, LV_ALIGN_BOTTOM_MID, 0, -5);

    lv_obj_t *weather = make_panel(tile, WATCH_SAFE_X + (card_w + gap) * 2, card_y, card_w, 44, 0x291C10);
    s_weather_chip_label = make_label(weather, "--C", font_body(), 0xE8A840);
    lv_obj_align(s_weather_chip_label, LV_ALIGN_TOP_MID, 0, 5);
    lv_obj_t *weather_cap = make_label(weather, "WEATHER", LV_FONT_DEFAULT, 0xA89080);
    lv_obj_align(weather_cap, LV_ALIGN_BOTTOM_MID, 0, -5);
}

// ==================== Page 1: Weather detail ====================
static void build_weather_page(lv_obj_t *tile)
{
    const lv_coord_t w = ui_w();
    const lv_coord_t tile_h = ui_h();

    // Light blue-white background
    set_plain_bg(tile, 0xEAF0F6);
    lv_obj_set_scrollbar_mode(tile, LV_SCROLLBAR_MODE_OFF);

    // --- Scrollable container ---
    lv_obj_t *scr = lv_obj_create(tile);
    lv_obj_set_size(scr, w, tile_h);
    lv_obj_set_pos(scr, 0, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(scr, LV_DIR_VER);

    // --- Top bar ---
    lv_obj_t *title = make_label(scr, "Weather", font_mid(), 0x1A2A3A);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, WATCH_SAFE_X, 12);

    lv_obj_t *city = make_label(scr, "Deyang", font_body(), 0x6B8299);
    lv_obj_align(city, LV_ALIGN_TOP_RIGHT, -WATCH_SAFE_X, 14);

    // --- Sun / Moon icon container ---
    lv_obj_t *icon_ct = lv_obj_create(scr);
    lv_obj_set_size(icon_ct, 52, 52);
    lv_obj_align(icon_ct, LV_ALIGN_TOP_MID, 0, 46);
    lv_obj_set_style_bg_opa(icon_ct, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(icon_ct, 0, 0);
    lv_obj_set_style_pad_all(icon_ct, 0, 0);

    // Sun: solid orange circle (no transparency issues)
    s_sun_glow = lv_obj_create(icon_ct);
    lv_obj_set_size(s_sun_glow, 46, 46);
    lv_obj_set_style_radius(s_sun_glow, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_sun_glow, lv_color_hex(0xFFB74D), 0);
    lv_obj_set_style_bg_opa(s_sun_glow, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_sun_glow, 0, 0);
    lv_obj_center(s_sun_glow);

    s_sun_core = lv_obj_create(icon_ct);
    lv_obj_set_size(s_sun_core, 30, 30);
    lv_obj_set_style_radius(s_sun_core, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_sun_core, lv_color_hex(0xFF9800), 0);
    lv_obj_set_style_bg_opa(s_sun_core, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_sun_core, 0, 0);
    lv_obj_center(s_sun_core);

    // Moon: single blue-gray circle (simple & distinct)
    s_moon_base = lv_obj_create(icon_ct);
    lv_obj_set_size(s_moon_base, 42, 42);
    lv_obj_set_style_radius(s_moon_base, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_moon_base, lv_color_hex(0x5C7A8A), 0);
    lv_obj_set_style_bg_opa(s_moon_base, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_moon_base, 0, 0);
    lv_obj_center(s_moon_base);
    lv_obj_add_flag(s_moon_base, LV_OBJ_FLAG_HIDDEN);

    // Moon shadow not used (moon is a simple solid circle)
    s_moon_shadow = NULL;

    // --- Temperature (48px) ---
    s_weather_temp_label = make_label(scr, "--C", font_time(), 0x1565C0);
    lv_obj_align(s_weather_temp_label, LV_ALIGN_TOP_MID, 0, 108);

    // --- Description + Feels (18px, two lines) ---
    s_weather_desc_label = make_label(scr, "Loading...\nFeels --C", font_body(), 0x5A7589);
    lv_obj_align(s_weather_desc_label, LV_ALIGN_TOP_MID, 0, 168);

    // --- Humidity ---
    s_weather_humidity_label = make_label(scr, "Humidity  --%", font_body(), 0x2196F3);
    lv_obj_align(s_weather_humidity_label, LV_ALIGN_TOP_MID, 0, 220);

    // --- Wind ---
    s_weather_wind_label = make_label(scr, "Wind  -- km/h", font_body(), 0x2196F3);
    lv_obj_align(s_weather_wind_label, LV_ALIGN_TOP_MID, 0, 244);

    // --- Refresh button ---
    s_refresh_btn = lv_btn_create(scr);
    lv_obj_set_size(s_refresh_btn, 110, 34);
    lv_obj_align(s_refresh_btn, LV_ALIGN_TOP_MID, 0, 274);
    lv_obj_set_style_bg_color(s_refresh_btn, lv_color_hex(0x2196F3), 0);
    lv_obj_set_style_bg_opa(s_refresh_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_refresh_btn, 0, 0);
    lv_obj_set_style_radius(s_refresh_btn, 12, 0);
    lv_obj_add_event_cb(s_refresh_btn, on_refresh_click, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_label = lv_label_create(s_refresh_btn);
    lv_label_set_text(btn_label, "Refresh");
    lv_obj_set_style_text_font(btn_label, font_body(), 0);
    lv_obj_set_style_text_color(btn_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(btn_label);

    // --- Update time ---
    s_weather_update_label = make_label(scr, "Updated: --:--", font_body(), 0x8CA0B3);
    lv_obj_align(s_weather_update_label, LV_ALIGN_TOP_MID, 0, 318);

    // --- Spacer ---
    lv_obj_t *spacer = lv_obj_create(scr);
    lv_obj_set_size(spacer, w, 4);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);
    lv_obj_align(spacer, LV_ALIGN_TOP_MID, 0, 344);
}

// ==================== Page 2: Timer / Stopwatch ====================
static void build_timer_page(lv_obj_t *tile)
{
    // Warm cream background
    set_plain_bg(tile, 0xF5F0EB);
    lv_obj_set_scrollbar_mode(tile, LV_SCROLLBAR_MODE_OFF);

    // Title
    lv_obj_t *title = make_label(tile, "Timer", font_mid(), 0x2C1A0A);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, WATCH_SAFE_X, 14);

    // Large time display
    s_timer_label = make_label(tile, "00:00.00", font_time(), 0x2C1A0A);
    lv_obj_align(s_timer_label, LV_ALIGN_CENTER, 0, -20);

    // Button 1: Start (left of center)
    s_timer_btn1 = lv_btn_create(tile);
    lv_obj_set_size(s_timer_btn1, 80, 80);
    lv_obj_align(s_timer_btn1, LV_ALIGN_BOTTOM_MID, -48, -40);
    lv_obj_set_style_bg_color(s_timer_btn1, lv_color_hex(0x4CAF50), 0);
    lv_obj_set_style_bg_opa(s_timer_btn1, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_timer_btn1, 0, 0);
    lv_obj_set_style_radius(s_timer_btn1, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_event_cb(s_timer_btn1, timer_btn1_cb, LV_EVENT_CLICKED, NULL);

    s_timer_btn1_label = lv_label_create(s_timer_btn1);
    lv_label_set_text(s_timer_btn1_label, "Start");
    lv_obj_set_style_text_font(s_timer_btn1_label, font_body(), 0);
    lv_obj_set_style_text_color(s_timer_btn1_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(s_timer_btn1_label);

    // Button 2: Reset (right of center)
    s_timer_btn2 = lv_btn_create(tile);
    lv_obj_set_size(s_timer_btn2, 80, 80);
    lv_obj_align(s_timer_btn2, LV_ALIGN_BOTTOM_MID, 48, -40);
    lv_obj_set_style_bg_color(s_timer_btn2, lv_color_hex(0xBDBDBD), 0);
    lv_obj_set_style_bg_opa(s_timer_btn2, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_timer_btn2, 0, 0);
    lv_obj_set_style_radius(s_timer_btn2, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_event_cb(s_timer_btn2, timer_btn2_cb, LV_EVENT_CLICKED, NULL);

    s_timer_btn2_label = lv_label_create(s_timer_btn2);
    lv_label_set_text(s_timer_btn2_label, "Reset");
    lv_obj_set_style_text_font(s_timer_btn2_label, font_body(), 0);
    lv_obj_set_style_text_color(s_timer_btn2_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(s_timer_btn2_label);
}

// ==================== Page dots ====================
static void update_page_dots(int active)
{
    const lv_coord_t gap = 16;
    const lv_coord_t total_h = gap * (WATCH_PAGE_COUNT - 1);
    const lv_coord_t y = (ui_h() - total_h) / 2;

    for (int i = 0; i < WATCH_PAGE_COUNT; i++) {
        if (s_page_dots[i] == NULL) continue;
        lv_obj_set_size(s_page_dots[i], i == active ? 14 : 5, 5);
        lv_obj_align(s_page_dots[i], LV_ALIGN_TOP_RIGHT, -8, y + i * gap);
        lv_obj_set_style_bg_color(s_page_dots[i],
                                  lv_color_hex(i == active ? 0x24D6C8 : 0x3B4655), 0);
    }
}

static void tile_event_cb(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED || s_tileview == NULL)
        return;

    lv_obj_t *active = lv_tileview_get_tile_act(s_tileview);
    for (int i = 0; i < WATCH_PAGE_COUNT; i++) {
        if (active == s_tiles[i]) {
            update_page_dots(i);
            break;
        }
    }
}

static void create_page_dots(lv_obj_t *parent)
{
    const lv_coord_t gap = 16;
    const lv_coord_t total_h = gap * (WATCH_PAGE_COUNT - 1);
    const lv_coord_t y = (ui_h() - total_h) / 2;

    for (int i = 0; i < WATCH_PAGE_COUNT; i++) {
        s_page_dots[i] = make_circle(parent, 5, 0x3B4655, LV_OPA_COVER);
        lv_obj_align(s_page_dots[i], LV_ALIGN_TOP_RIGHT, -8, y + i * gap);
    }
    update_page_dots(0);
}

// ==================== 1-second tick ====================
static void watch_tick_cb(lv_timer_t *timer)
{
    (void)timer;

    // Real time from NTP (falls back to boot time before sync)
    time_t now_t = time(NULL);
    struct tm *tm_info = localtime(&now_t);
    const int hour   = tm_info->tm_hour;
    const int minute = tm_info->tm_min;
    const int second = tm_info->tm_sec;
    const int wday   = tm_info->tm_wday;  // 0=Sun..6=Sat
    const int mday   = tm_info->tm_mday;
    const int mon    = tm_info->tm_mon;   // 0=Jan..11=Dec

    // Simulated health data (kept for demo)
    const uint32_t elapsed = (uint32_t)(esp_timer_get_time() / 1000000ULL);
    const int steps = 7842 + (int)((elapsed * 7U) % 1800U);
    const int steps_pct = (steps % 10000) / 100;
    const int heart = 68 + (int)((elapsed / 4U) % 11U);
    const int battery = 86 - (int)((elapsed / 900U) % 8U);

    // --- Watch face updates ---
    if (s_time_label) {
        lv_label_set_text_fmt(s_time_label, "%02d:%02d", hour, minute);
    }
    if (s_second_label) {
        lv_label_set_text_fmt(s_second_label, "%02d", second);
    }
    if (s_date_label) {
        const char *wdays[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
        const char *months[] = {"JAN","FEB","MAR","APR","MAY","JUN",
                                "JUL","AUG","SEP","OCT","NOV","DEC"};
        lv_label_set_text_fmt(s_date_label, "%s  %s %d",
                              wdays[wday], months[mon], mday);
    }
    if (s_battery_label) {
        lv_label_set_text_fmt(s_battery_label, "%d%%", battery);
    }
    if (s_steps_chip_label) {
        lv_label_set_text_fmt(s_steps_chip_label, "%d", steps);
    }
    if (s_heart_chip_label) {
        lv_label_set_text_fmt(s_heart_chip_label, "%d", heart);
    }
    if (s_main_arc) {
        lv_arc_set_value(s_main_arc, steps_pct);
    }

    // --- Weather: read from real data ---
    const weather_data_t *w = weather_get_current();
    char buf[64];

    // Sun (6:00-18:00) / Moon (18:00-6:00) toggle
    bool is_day = (hour >= 6 && hour < 18);
    show_sun(is_day);
    show_moon(!is_day);

    // Chip label (page 0) — 1 decimal
    if (s_weather_chip_label) {
        if (w->valid) {
            snprintf(buf, sizeof(buf), "%.1fC", w->temperature);
            lv_label_set_text(s_weather_chip_label, buf);
        } else {
            lv_label_set_text(s_weather_chip_label, "--C");
        }
    }
    // Big temperature — 1 decimal
    if (s_weather_temp_label) {
        if (w->valid) {
            snprintf(buf, sizeof(buf), "%.1fC", w->temperature);
            lv_label_set_text(s_weather_temp_label, buf);
        }
    }
    // Description + feels like
    if (s_weather_desc_label && w->valid) {
        snprintf(buf, sizeof(buf), "%s\nFeels %.1fC",
                 weather_code_to_text(w->weather_code),
                 w->temperature + 2.0f);
        lv_label_set_text(s_weather_desc_label, buf);
    }
    // Humidity (integer, use lv_label_set_text_fmt)
    if (s_weather_humidity_label) {
        lv_label_set_text_fmt(s_weather_humidity_label, "Humidity  %d%%",
                              w->valid ? w->humidity : 0);
    }
    // Wind speed — 1 decimal
    if (s_weather_wind_label) {
        snprintf(buf, sizeof(buf), "Wind  %.1f km/h",
                 w->valid ? w->wind_speed : 0.0f);
        lv_label_set_text(s_weather_wind_label, buf);
    }
    // Update time
    if (s_weather_update_label) {
        lv_label_set_text_fmt(s_weather_update_label, "Updated: %s",
                              weather_get_update_time());
    }
    // Refresh button flash reset
    if (s_refresh_btn) {
        lv_obj_set_style_bg_color(s_refresh_btn, lv_color_hex(0x2196F3), 0);
    }
}

// --- Game scroll lock (called by 2048 game) ---
static void game_scroll_lock(bool lock)
{
    if (s_tileview) {
        lv_obj_set_scroll_dir(s_tileview, lock ? LV_DIR_NONE : LV_DIR_HOR);
    }
}

// ==================== Public entry ====================
void watch_ui_create(void)
{
    if (s_update_timer != NULL) {
        lv_timer_del(s_update_timer);
        s_update_timer = NULL;
    }

    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    set_plain_bg(scr, 0x1C110A);

    // TileView with 2 pages
    s_tileview = lv_tileview_create(scr);
    lv_obj_set_size(s_tileview, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
    lv_obj_set_style_bg_opa(s_tileview, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_tileview, 0, 0);
    lv_obj_set_style_pad_all(s_tileview, 0, 0);
    lv_obj_set_scrollbar_mode(s_tileview, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(s_tileview, tile_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_tiles[0] = lv_tileview_add_tile(s_tileview, 0, 0, LV_DIR_RIGHT);
    s_tiles[1] = lv_tileview_add_tile(s_tileview, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    s_tiles[2] = lv_tileview_add_tile(s_tileview, 2, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    s_tiles[3] = lv_tileview_add_tile(s_tileview, 3, 0, LV_DIR_LEFT);

    build_watch_face(s_tiles[0]);
    build_weather_page(s_tiles[1]);
    build_timer_page(s_tiles[2]);
    game_2048_create(s_tiles[3]);


    create_page_dots(scr);
    lv_obj_set_tile_id(s_tileview, 0, 0, LV_ANIM_OFF);

    watch_tick_cb(NULL);
    s_update_timer = lv_timer_create(watch_tick_cb, 1000, NULL);

    game_2048_set_lock_cb(game_scroll_lock);
}
