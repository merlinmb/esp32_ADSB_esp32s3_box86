#include "display.h"
#include "merlinRadarMap.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <TAMC_GT911.h>
#include <WiFi.h>
#include <lvgl.h>
#include <TimeLib.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <esp_heap_caps.h>

#include "ui_fonts.h"

// ST7701 setup for the ESP32-4848S040 RGB panel.
static const uint8_t st7701_init_corrected[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x10,

    WRITE_C8_D16, 0xC0, 0x3B, 0x00,
    WRITE_C8_D16, 0xC1, 0x0D, 0x02,
    WRITE_C8_D16, 0xC2, 0x31, 0x05,
    WRITE_C8_D8, 0xCD, 0x00,

    WRITE_COMMAND_8, 0xB0, // Positive Voltage Gamma Control
    WRITE_BYTES, 16,
    0x00, 0x11, 0x18, 0x0E,
    0x11, 0x06, 0x07, 0x08,
    0x07, 0x22, 0x04, 0x12,
    0x0F, 0xAA, 0x31, 0x18,

    WRITE_COMMAND_8, 0xB1, // Negative Voltage Gamma Control
    WRITE_BYTES, 16,
    0x00, 0x11, 0x19, 0x0E,
    0x12, 0x07, 0x08, 0x08,
    0x08, 0x22, 0x04, 0x11,
    0x11, 0xA9, 0x32, 0x18,

    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x11,

    WRITE_C8_D8, 0xB0, 0x60,
    WRITE_C8_D8, 0xB1, 0x32,
    WRITE_C8_D8, 0xB2, 0x07,
    WRITE_C8_D8, 0xB3, 0x80,
    WRITE_C8_D8, 0xB5, 0x49,
    WRITE_C8_D8, 0xB7, 0x85,
    WRITE_C8_D8, 0xB8, 0x21,
    WRITE_C8_D8, 0xC1, 0x78,
    WRITE_C8_D8, 0xC2, 0x78,

    WRITE_COMMAND_8, 0xE0,
    WRITE_BYTES, 3, 0x00, 0x1B, 0x02,

    WRITE_COMMAND_8, 0xE1,
    WRITE_BYTES, 11,
    0x08, 0xA0, 0x00, 0x00,
    0x07, 0xA0, 0x00, 0x00,
    0x00, 0x44, 0x44,

    WRITE_COMMAND_8, 0xE2,
    WRITE_BYTES, 12,
    0x11, 0x11, 0x44, 0x44,
    0xED, 0xA0, 0x00, 0x00,
    0xEC, 0xA0, 0x00, 0x00,

    WRITE_COMMAND_8, 0xE3,
    WRITE_BYTES, 4, 0x00, 0x00, 0x11, 0x11,

    WRITE_C8_D16, 0xE4, 0x44, 0x44,

    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 16,
    0x0A, 0xE9, 0xD8, 0xA0,
    0x0C, 0xEB, 0xD8, 0xA0,
    0x0E, 0xED, 0xD8, 0xA0,
    0x10, 0xEF, 0xD8, 0xA0,

    WRITE_COMMAND_8, 0xE6,
    WRITE_BYTES, 4, 0x00, 0x00, 0x11, 0x11,

    WRITE_C8_D16, 0xE7, 0x44, 0x44,

    WRITE_COMMAND_8, 0xE8,
    WRITE_BYTES, 16,
    0x09, 0xE8, 0xD8, 0xA0,
    0x0B, 0xEA, 0xD8, 0xA0,
    0x0D, 0xEC, 0xD8, 0xA0,
    0x0F, 0xEE, 0xD8, 0xA0,

    WRITE_COMMAND_8, 0xEB,
    WRITE_BYTES, 7,
    0x02, 0x00, 0xE4, 0xE4,
    0x88, 0x00, 0x40,

    WRITE_C8_D16, 0xEC, 0x3C, 0x00,

    WRITE_COMMAND_8, 0xED,
    WRITE_BYTES, 16,
    0xAB, 0x89, 0x76, 0x54,
    0x02, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x20,
    0x45, 0x67, 0x98, 0xBA,

    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x13,

    WRITE_C8_D8, 0xE5, 0xE4,

    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x00,

    WRITE_C8_D8, 0x3A, 0x60, // RGB666, required by the ESP32-4848S040 RGB interface

    WRITE_COMMAND_8, 0x11, // Sleep Out
    END_WRITE,

    DELAY, 120,

    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x29, // Display On
    END_WRITE,
};

// ── Pin definitions ────────────────────────────────────────────────────────

#define GFX_BL 38

// Touch (GT911)
#define TOUCH_SCL 45
#define TOUCH_SDA 19
#define TOUCH_INT -1
#define TOUCH_RST -1
#define TOUCH_MAP_X1 480
#define TOUCH_MAP_X2 0
#define TOUCH_MAP_Y1 480
#define TOUCH_MAP_Y2 0

// ── Arduino_GFX display ────────────────────────────────────────────────────

static Arduino_DataBus *s_spibus = new Arduino_SWSPI(
    GFX_NOT_DEFINED /* DC */, 39 /* CS */, 48 /* SCK */, 47 /* MOSI */);

static Arduino_ESP32RGBPanel *s_rgb_bus = new Arduino_ESP32RGBPanel(
    18 /* DE */, 17 /* VSYNC */, 16 /* HSYNC */, 21 /* PCLK */,
    11 /* R0 */, 12 /* R1 */, 13 /* R2 */, 14 /* R3 */, 0  /* R4 */,
    8  /* G0 */, 20 /* G1 */, 3  /* G2 */, 46 /* G3 */, 9  /* G4 */, 10 /* G5 */,
    4  /* B0 */, 5  /* B1 */, 6  /* B2 */, 7  /* B3 */, 15 /* B4 */,
    1  /* hsync_polarity */, 10 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 50 /* hsync_back_porch */,
    1  /* vsync_polarity */, 10 /* vsync_front_porch */, 8 /* vsync_pulse_width */, 20 /* vsync_back_porch */,
    0  /* pclk_active_neg */, 12000000 /* prefer_speed */, false /* useBigEndian */,
    0  /* de_idle_high */, 0 /* pclk_idle_high */, DISPLAY_WIDTH * 10 /* bounce buffer pixels */);

static Arduino_RGB_Display *s_gfx = new Arduino_RGB_Display(
    DISPLAY_WIDTH, DISPLAY_HEIGHT, s_rgb_bus, 0 /* rotation */, true /* auto_flush */,
    s_spibus, GFX_NOT_DEFINED /* RST */,
    st7701_init_corrected, sizeof(st7701_init_corrected));

// ── Touch ──────────────────────────────────────────────────────────────────

static TAMC_GT911 s_ts(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST,
                       max(TOUCH_MAP_X1, TOUCH_MAP_X2),
                       max(TOUCH_MAP_Y1, TOUCH_MAP_Y2));

static int s_touch_x = 0, s_touch_y = 0;

// ── LVGL draw buffer / driver ──────────────────────────────────────────────

static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t      s_disp_drv;
static bool               s_display_ready = false;

// ── LVGL callbacks ────────────────────────────────────────────────────────

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;

    uint16_t *fb = s_gfx->getFramebuffer();
    uint16_t *src = (uint16_t *)&color_p->full;

    if (area->x1 == 0 && w == DISPLAY_WIDTH) {
        memcpy(fb + area->y1 * DISPLAY_WIDTH, src, w * h * 2);
        Cache_WriteBack_Addr((uint32_t)(fb + area->y1 * DISPLAY_WIDTH), w * h * 2);
    } else {
        for (uint32_t row = 0; row < h; row++) {
            memcpy(fb + (area->y1 + row) * DISPLAY_WIDTH + area->x1, src + row * w, w * 2);
        }
        Cache_WriteBack_Addr((uint32_t)(fb + area->y1 * DISPLAY_WIDTH + area->x1), (h - 1) * DISPLAY_WIDTH * 2 + w * 2);
    }

    lv_disp_flush_ready(drv);
}

// The GT911 has no wired RST/INT (TOUCH_RST/TOUCH_INT = -1), so it isn't
// hardware-reset at boot. If it ever comes up (or the I2C peripheral gets
// left) in a bad state, every Wire transaction fails with
// ESP_ERR_INVALID_STATE forever, since nothing else re-inits the bus.
// Wire.begin() re-drives SDA/SCL and resets the I2C peripheral's internal
// state machine, so re-running it after a run of consecutive failures
// recovers a wedged bus without needing a physical reset line.
static void recover_i2c_bus_if_wedged() {
    static uint8_t s_consecutive_failures = 0;
    constexpr uint8_t RECOVER_AFTER_FAILURES = 5;

    Wire.beginTransmission(GT911_ADDR1);
    uint8_t err = Wire.endTransmission();

    if (err == 0) {
        s_consecutive_failures = 0;
        return;
    }

    if (++s_consecutive_failures >= RECOVER_AFTER_FAILURES) {
        Wire.end();
        Wire.begin(TOUCH_SDA, TOUCH_SCL);
        s_consecutive_failures = 0;
    }
}

static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    static bool          s_was_touched  = false;
    static unsigned long s_touch_down_ms = 0;
    static constexpr unsigned long DEBOUNCE_MS = 50;

    recover_i2c_bus_if_wedged();
    s_ts.read();
    if (s_ts.isTouched) {
        unsigned long now_ms = millis();
        if (!s_was_touched) {
            s_touch_down_ms = now_ms;
            s_was_touched   = true;
        }
        s_touch_x = map(s_ts.points[0].x, TOUCH_MAP_X1, TOUCH_MAP_X2, 0, DISPLAY_WIDTH - 1);
        s_touch_y = map(s_ts.points[0].y, TOUCH_MAP_Y1, TOUCH_MAP_Y2, 0, DISPLAY_HEIGHT - 1);
        // Only report as pressed after debounce period
        if (now_ms - s_touch_down_ms >= DEBOUNCE_MS) {
            data->state   = LV_INDEV_STATE_PR;
            data->point.x = s_touch_x;
            data->point.y = s_touch_y;
        } else {
            data->state   = LV_INDEV_STATE_REL;
            data->point.x = s_touch_x;
            data->point.y = s_touch_y;
        }
    } else if (s_touch_x || s_touch_y) {
        data->state   = LV_INDEV_STATE_REL;
        data->point.x = s_touch_x;
        data->point.y = s_touch_y;
        s_was_touched = false;
        s_touch_x = 0;
        s_touch_y = 0;
    } else {
        data->state   = LV_INDEV_STATE_REL;
        s_was_touched = false;
    }
}

// ── UI state ──────────────────────────────────────────────────────────────

namespace {

const lv_color_t COLOR_BG       = lv_color_hex(0x000000);
const lv_color_t COLOR_TEXT_1   = lv_color_hex(0xe8eaf2);
const lv_color_t COLOR_TEXT_2   = lv_color_hex(0x8b90aa);
const lv_color_t COLOR_TEXT_3   = lv_color_hex(0x50546a);
const lv_color_t COLOR_ACCENT   = lv_color_hex(0x5b8ef0);
const lv_color_t COLOR_RED      = lv_color_hex(0xe0402a);
const lv_color_t COLOR_GREEN    = lv_color_hex(0x35c46a);
const lv_color_t COLOR_YELLOW   = lv_color_hex(0xffd23f);
const lv_color_t COLOR_CYAN     = lv_color_hex(0x00e5ff);
const lv_color_t COLOR_MAGENTA  = lv_color_hex(0xd23fff);
const lv_color_t COLOR_CLOCK    = lv_color_hex(0xffffff);

lv_obj_t *s_root         = nullptr;
lv_obj_t *s_content       = nullptr;
lv_obj_t *s_clock_label   = nullptr;
lv_obj_t *s_touch_zone_left  = nullptr;
lv_obj_t *s_touch_zone_right = nullptr;
lv_obj_t *s_startup_log      = nullptr;
lv_obj_t *s_radar_info_box   = nullptr;
lv_timer_t *s_radar_info_timer = nullptr;
bool s_startup_active        = true;
constexpr uint8_t STARTUP_LOG_LINES = 14;
String s_startup_lines[STARTUP_LOG_LINES];
constexpr int TOUCH_ZONE_SIZE = 32;

// Double-tap detection state, mirrors the debounce style used for GT911 reads.
unsigned long s_last_left_tap_ms  = 0;
unsigned long s_last_right_tap_ms = 0;
constexpr unsigned long DOUBLE_TAP_WINDOW_MS = 400;

lv_obj_t *create_label(lv_obj_t *parent, const lv_font_t *font,
                        lv_color_t color, const char *text) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_label_set_text(lbl, text);
    return lbl;
}

void left_zone_click_cb(lv_event_t *e) {
    unsigned long now_ms = millis();
    if (now_ms - s_last_left_tap_ms <= DOUBLE_TAP_WINDOW_MS) {
        s_last_left_tap_ms = 0;
        onTouchToggleSysInfo();
    } else {
        s_last_left_tap_ms = now_ms;
        onTouchRotateBrightness();
    }
}

void right_zone_click_cb(lv_event_t *e) {
    unsigned long now_ms = millis();
    if (now_ms - s_last_right_tap_ms <= DOUBLE_TAP_WINDOW_MS) {
        s_last_right_tap_ms = 0;
        onTouchTriggerFetch();
    } else {
        s_last_right_tap_ms = now_ms;
        onTouchAdvanceFrame();
    }
}

void left_zone_long_press_cb(lv_event_t *e) {
    onTouchReboot();
}

void create_touch_zones() {
    s_touch_zone_left = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_touch_zone_left);
    lv_obj_set_size(s_touch_zone_left, TOUCH_ZONE_SIZE, TOUCH_ZONE_SIZE);
    lv_obj_align(s_touch_zone_left, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_add_flag(s_touch_zone_left, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_touch_zone_left, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_touch_zone_left, left_zone_click_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(s_touch_zone_left, left_zone_long_press_cb, LV_EVENT_LONG_PRESSED, nullptr);

    s_touch_zone_right = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_touch_zone_right);
    lv_obj_set_size(s_touch_zone_right, TOUCH_ZONE_SIZE, TOUCH_ZONE_SIZE);
    lv_obj_align(s_touch_zone_right, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_flag(s_touch_zone_right, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_touch_zone_right, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_touch_zone_right, right_zone_click_cb, LV_EVENT_CLICKED, nullptr);
}

lv_color_t altitude_color(int altitude) {
    uint8_t r, g, b;
    if (altitude <= 2000) {
        r = 255; g = 165; b = 0;
    } else if (altitude <= 6000) {
        float factor = (altitude - 2000) / 4000.0f;
        r = 255; g = (uint8_t)(165 + factor * (255 - 165)); b = 0;
    } else if (altitude <= 10000) {
        float factor = (altitude - 6000) / 4000.0f;
        r = (uint8_t)(255 - factor * 255); g = 255; b = (uint8_t)(factor * 255);
    } else if (altitude <= 20000) {
        float factor = (altitude - 10000) / 10000.0f;
        r = 0; g = (uint8_t)(255 - factor * 255); b = 255;
    } else if (altitude <= 30000) {
        float factor = (altitude - 20000) / 10000.0f;
        r = (uint8_t)(factor * 75); g = 0; b = (uint8_t)(255 - factor * (255 - 130));
    } else {
        float factor = (altitude - 30000) / 10000.0f;
        r = (uint8_t)(75 + factor * (238 - 75)); g = (uint8_t)(factor * 130); b = (uint8_t)(130 + factor * (238 - 130));
    }
    return lv_color_make(r, g, b);
}

lv_color_t status_color(const String &status) {
    String s = status;
    s.toLowerCase();
    if (s == "cruising") return lv_color_hex(0xffa500);
    if (s == "descending") return COLOR_RED;
    if (s == "ascending") return COLOR_GREEN;
    return COLOR_TEXT_1;
}

void radar_stop(); // fwd decl; defined near the radar screen implementation below
void dismiss_radar_info();

void clear_content(bool keep_radar_info = false) {
    radar_stop(); // pause the sweep timer before its canvas objects are deleted below
    if (!keep_radar_info) dismiss_radar_info();
    if (s_content) {
        lv_obj_del(s_content);
        s_content = nullptr;
    }
}

void clear_startup_log() {
    s_startup_active = false;
    if (s_startup_log) {
        lv_obj_del(s_startup_log);
        s_startup_log = nullptr;
    }
}

lv_obj_t *begin_content(bool keep_radar_info = false) {
    clear_startup_log();
    clear_content(keep_radar_info);
    s_content = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_size(s_content, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_clear_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_background(s_content); // keep touch zones on top for input
    return s_content;
}

void row_label(lv_obj_t *parent, int y, const char *label, const char *value, lv_color_t value_color) {
    lv_obj_t *l = create_label(parent, &font_inter_regular_14, COLOR_TEXT_2, label);
    lv_obj_set_pos(l, 20, y);
    lv_obj_t *v = create_label(parent, &font_inter_semibold_14, value_color, value);
    lv_obj_set_pos(v, 200, y);
}

void render_screen_header(lv_obj_t *parent, const char *eyebrow, const char *title) {
    lv_obj_t *accent = lv_obj_create(parent);
    lv_obj_remove_style_all(accent);
    lv_obj_set_size(accent, 5, 42);
    lv_obj_set_pos(accent, 20, 22);
    lv_obj_set_style_bg_color(accent, COLOR_CYAN, 0);

    lv_obj_t *eyebrow_label = create_label(parent, &font_inter_regular_12, COLOR_TEXT_2, eyebrow);
    lv_obj_set_pos(eyebrow_label, 38, 21);
    lv_obj_t *title_label = create_label(parent, &font_inter_bold_18, COLOR_TEXT_1, title);
    lv_obj_set_pos(title_label, 38, 38);

    lv_obj_t *rule = lv_obj_create(parent);
    lv_obj_remove_style_all(rule);
    lv_obj_set_size(rule, DISPLAY_WIDTH - 40, 1);
    lv_obj_set_pos(rule, 20, 78);
    lv_obj_set_style_bg_color(rule, COLOR_TEXT_3, 0);
}

lv_obj_t *create_metric_panel(lv_obj_t *parent, int x, int y, int width, int height) {
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, width, height);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x111520), 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x252c3e), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 4, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

void metric_label(lv_obj_t *parent, int x, int y, const char *label, const char *value, lv_color_t value_color) {
    lv_obj_t *label_obj = create_label(parent, &font_inter_regular_12, COLOR_TEXT_2, label);
    lv_obj_set_pos(label_obj, x, y);
    lv_obj_t *value_obj = create_label(parent, &font_inter_semibold_14, value_color, value);
    lv_obj_set_pos(value_obj, x, y + 17);
}

const char *status_indicator(const String &status) {
    String normalized = status;
    normalized.toLowerCase();
    if (normalized == "ascending") return "UP";
    if (normalized == "descending") return "DOWN";
    return "LEVEL";
}

float aircraft_bearing_degrees(const AircraftDetailsStruct &aircraft) {
    const float latitude_delta = radians(aircraft.latitude - myLat);
    const float longitude_delta = radians(aircraft.longitude - myLon);
    const float origin_latitude = radians(myLat);
    const float aircraft_latitude = radians(aircraft.latitude);

    const float y = sinf(longitude_delta) * cosf(aircraft_latitude);
    const float x = cosf(origin_latitude) * sinf(aircraft_latitude) -
                    sinf(origin_latitude) * cosf(aircraft_latitude) * cosf(longitude_delta);
    float bearing = degrees(atan2f(y, x));
    return bearing < 0.0f ? bearing + 360.0f : bearing;
}

const char *compass_direction(float bearing) {
    static constexpr const char *DIRECTIONS[] = {
        "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
        "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"
    };
    return DIRECTIONS[(int)((bearing + 11.25f) / 22.5f) % 16];
}

void render_look_direction(lv_obj_t *parent, const AircraftDetailsStruct &aircraft) {
    lv_obj_t *panel = create_metric_panel(parent, 20, 316, 440, 124);

    static lv_color_t *compass_buffer = nullptr;
    constexpr int COMPASS_SIZE = 104;
    if (!compass_buffer) {
        compass_buffer = (lv_color_t *)heap_caps_malloc(
            LV_CANVAS_BUF_SIZE_TRUE_COLOR(COMPASS_SIZE, COMPASS_SIZE), MALLOC_CAP_SPIRAM);
    }

    const bool has_position = isfinite(aircraft.latitude) && isfinite(aircraft.longitude) &&
                              isfinite(aircraft.distance) && aircraft.distance > 0.0f;
    const float bearing = has_position ? aircraft_bearing_degrees(aircraft) : 0.0f;
    const float elevation = has_position
        ? degrees(atan2f((float)aircraft.altitude, aircraft.distance * 6076.12f))
        : 0.0f;

    if (compass_buffer) {
        lv_obj_t *canvas = lv_canvas_create(panel);
        lv_canvas_set_buffer(canvas, compass_buffer, COMPASS_SIZE, COMPASS_SIZE, LV_IMG_CF_TRUE_COLOR);
        lv_canvas_fill_bg(canvas, lv_color_hex(0x111520), LV_OPA_COVER);
        lv_obj_set_pos(canvas, 10, 10);

        constexpr int CENTER = COMPASS_SIZE / 2;
        lv_draw_arc_dsc_t ring_dsc;
        lv_draw_arc_dsc_init(&ring_dsc);
        ring_dsc.color = COLOR_TEXT_3;
        ring_dsc.width = 1;
        lv_canvas_draw_arc(canvas, CENTER, CENTER, 35, 0, 360, &ring_dsc);

        lv_draw_line_dsc_t pointer_dsc;
        lv_draw_line_dsc_init(&pointer_dsc);
        pointer_dsc.color = has_position ? COLOR_CYAN : COLOR_TEXT_3;
        pointer_dsc.width = 3;
        const float pointer_angle = radians(bearing);
        const int tip_x = CENTER + (int)(29.0f * sinf(pointer_angle));
        const int tip_y = CENTER - (int)(29.0f * cosf(pointer_angle));
        lv_point_t pointer[2] = {{CENTER, CENTER}, {(lv_coord_t)tip_x, (lv_coord_t)tip_y}};
        lv_canvas_draw_line(canvas, pointer, 2, &pointer_dsc);
    }

    lv_obj_t *north = create_label(panel, &font_inter_bold_16, COLOR_TEXT_1, "N");
    lv_obj_set_pos(north, 56, 4);
    lv_obj_t *east = create_label(panel, &font_inter_regular_12, COLOR_TEXT_2, "E");
    lv_obj_set_pos(east, 99, 51);
    lv_obj_t *south = create_label(panel, &font_inter_regular_12, COLOR_TEXT_2, "S");
    lv_obj_set_pos(south, 57, 99);
    lv_obj_t *west = create_label(panel, &font_inter_regular_12, COLOR_TEXT_2, "W");
    lv_obj_set_pos(west, 9, 51);

    char direction_text[20];
    char elevation_text[24];
    if (has_position) {
        snprintf(direction_text, sizeof(direction_text), "%s  %03d DEG", compass_direction(bearing), (int)roundf(bearing));
        snprintf(elevation_text, sizeof(elevation_text), "%+.0f DEG", elevation);
    } else {
        snprintf(direction_text, sizeof(direction_text), "POSITION N/A");
        snprintf(elevation_text, sizeof(elevation_text), "N/A");
    }

    metric_label(panel, 136, 18, "LOOK DIRECTION", direction_text, has_position ? COLOR_CYAN : COLOR_TEXT_3);
    metric_label(panel, 136, 68, "ABOVE HORIZON", elevation_text, has_position ? COLOR_GREEN : COLOR_TEXT_3);
}

void render_sysinfo(lv_obj_t *parent, const FlightStats &stats) {
    (void)stats;
    render_screen_header(parent, "SYSTEM STATUS", "Aircraft Tracker");

    char rssiStr[8];
    snprintf(rssiStr, sizeof(rssiStr), "%d%%", (int)WiFi.RSSI());
    char heapStr[16];
    snprintf(heapStr, sizeof(heapStr), "%u", (unsigned)ESP.getFreeHeap());

    lv_obj_t *network_panel = create_metric_panel(parent, 20, 98, 440, 92);
    metric_label(network_panel, 16, 14, "NETWORK", WiFi.SSID().c_str(), COLOR_TEXT_1);
    metric_label(network_panel, 230, 14, "SIGNAL", rssiStr, COLOR_CYAN);
    metric_label(network_panel, 16, 54, "ADDRESS", WiFi.localIP().toString().c_str(), COLOR_TEXT_1);

    lv_obj_t *memory_panel = create_metric_panel(parent, 20, 204, 440, 58);
    metric_label(memory_panel, 16, 13, "FREE MEMORY", heapStr, COLOR_GREEN);

    lv_obj_t *credit = create_label(parent, &font_inter_regular_12, COLOR_TEXT_3, "ADS-B FLIGHT MONITOR");
    lv_obj_set_pos(credit, 20, 284);
}

void render_topstats(lv_obj_t *parent, const FlightStats &stats) {
    render_screen_header(parent, "LIVE AIRSPACE", "Flight Statistics");

    const AircraftDetailsStruct &fastest  = stats.aircraft[stats.fastestAircraft];
    const AircraftDetailsStruct &slowest  = stats.aircraft[stats.slowestAircraft];
    const AircraftDetailsStruct &highest  = stats.aircraft[stats.highestAircraft];
    const AircraftDetailsStruct &lowest   = stats.aircraft[stats.lowestAircraft];
    const AircraftDetailsStruct &closest  = stats.aircraft[stats.closestAircraft];
    const AircraftDetailsStruct &farthest = stats.aircraft[stats.farthestAircraft];

    auto stats_row = [parent](int y, const char *label, const String &identifier,
                              const String &value, lv_color_t value_color) {
        lv_obj_t *panel = create_metric_panel(parent, 20, y, 440, 34);
        lv_obj_t *label_obj = create_label(panel, &font_inter_regular_12, COLOR_TEXT_2, label);
        lv_obj_set_pos(label_obj, 12, 9);
        lv_obj_t *identifier_obj = create_label(parent, &font_inter_semibold_14, COLOR_TEXT_1, identifier.c_str());
        lv_obj_set_pos(identifier_obj, 160, y + 8);
        lv_obj_t *value_obj = create_label(parent, &font_inter_semibold_14, value_color, value.c_str());
        lv_obj_set_width(value_obj, 110);
        lv_obj_set_style_text_align(value_obj, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(value_obj, 338, y + 8);
    };

    stats_row(96,  "FASTEST",  fastest.identifier,  String((int)fastest.speed) + " KT", COLOR_CYAN);
    stats_row(136, "SLOWEST",  slowest.identifier,  String((int)slowest.speed) + " KT", COLOR_CYAN);
    stats_row(176, "HIGHEST",  highest.identifier,  String((int)highest.altitude) + " FT", COLOR_CYAN);
    stats_row(216, "LOWEST",   lowest.identifier,   String((int)lowest.altitude) + " FT", COLOR_CYAN);
    stats_row(256, "CLOSEST",  closest.identifier,  String((int)closest.distance) + " NMI", COLOR_CYAN);
    stats_row(296, "FARTHEST", farthest.identifier, String((int)farthest.distance) + " NMI", COLOR_CYAN);
    stats_row(336, "EMERGENCY", "", stats.emergencyCount > 0 ? String(stats.emergencyCount) : "NONE",
              stats.emergencyCount > 0 ? COLOR_RED : COLOR_GREEN);
}

void render_aircraft_card(lv_obj_t *parent, const FlightStats &stats, const AircraftDetailsStruct &aircraft, bool is_emergency) {
    if (is_emergency) {
        lv_obj_set_style_border_color(parent, COLOR_RED, 0);
        lv_obj_set_style_border_width(parent, 4, 0);
    }

    render_screen_header(parent, is_emergency ? "PRIORITY AIRCRAFT" : "CLOSEST FLIGHT", "Tracking");

    if (stats.totalAircraft > 0) {
        constexpr int badge_w = 132;
        constexpr int badge_h = 66;
        lv_obj_t *badge = lv_obj_create(parent);
        lv_obj_set_size(badge, badge_w, badge_h);
        lv_obj_set_pos(badge, DISPLAY_WIDTH - badge_w - 20, 20);
        lv_obj_set_style_bg_color(badge, COLOR_MAGENTA, 0);
        lv_obj_set_style_radius(badge, 6, 0);
        lv_obj_set_style_pad_all(badge, 0, 0);
        lv_obj_t *tracking = create_label(badge, &font_inter_regular_12, lv_color_black(), "TRACKING");
        lv_obj_set_width(tracking, badge_w);
        lv_obj_set_style_text_align(tracking, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(tracking, 0, 8);
        lv_obj_t *count = create_label(badge, &font_inter_bold_18, lv_color_black(), String(stats.totalAircraft).c_str());
        lv_obj_set_width(count, badge_w);
        lv_obj_set_style_text_align(count, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(count, 0, 26);
    }

    lv_obj_t *identity_panel = create_metric_panel(parent, 20, 98, 440, 82);
    if (aircraft.identifier.length() > 0) {
        lv_obj_t *l = create_label(identity_panel, &font_inter_bold_18, COLOR_TEXT_1, aircraft.identifier.c_str());
        lv_obj_set_pos(l, 16, 13);
    }

    if (aircraft.description.length() > 0) {
        lv_obj_t *l = create_label(identity_panel, &font_inter_regular_14, COLOR_TEXT_2, aircraft.description.c_str());
        lv_obj_set_pos(l, 16, 46);
    }

    lv_obj_t *metrics_panel = create_metric_panel(parent, 20, 194, 440, 108);
    metric_label(metrics_panel, 16, 14, "DISTANCE", (String((int)aircraft.distance) + " NMI").c_str(), COLOR_CYAN);
    metric_label(metrics_panel, 230, 14, "ALTITUDE", (String((int)aircraft.altitude) + " FT").c_str(), altitude_color(aircraft.altitude));
    metric_label(metrics_panel, 16, 62, "SQUAWK", String(aircraft.squawk).c_str(),
                 isSquawkEmergency(aircraft.squawk) ? COLOR_RED : COLOR_TEXT_1);

    lv_obj_t *status_chip = lv_obj_create(metrics_panel);
    lv_obj_set_size(status_chip, 174, 34);
    lv_obj_set_pos(status_chip, 230, 59);
    lv_obj_set_style_bg_color(status_chip, status_color(aircraft.status), 0);
    lv_obj_set_style_radius(status_chip, 3, 0);
    lv_obj_set_style_pad_all(status_chip, 0, 0);
    lv_obj_t *status = create_label(status_chip, &font_inter_bold_16, lv_color_black(), status_indicator(aircraft.status));
    lv_obj_set_pos(status, 12, 8);
    lv_obj_t *status_text = create_label(status_chip, &font_inter_regular_12, lv_color_black(), aircraft.status.c_str());
    lv_obj_set_width(status_text, 102);
    lv_obj_set_style_text_align(status_text, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(status_text, 60, 10);

    if (!is_emergency) {
        render_look_direction(parent, aircraft);
    }
}

void render_empty(lv_obj_t *parent) {
    // Burn-in prevention: bounce the message vertically over time.
    static int offset = 0;
    static int dir = 1;
    constexpr int BOUNCE_STEP = 20;
    constexpr int BOUNCE_MAX = DISPLAY_HEIGHT - 120;
    offset += BOUNCE_STEP * dir;
    if (offset >= BOUNCE_MAX) { offset = BOUNCE_MAX; dir = -1; }
    else if (offset <= 0) { offset = 0; dir = 1; }

    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, DISPLAY_WIDTH - 48, 116);
    lv_obj_set_pos(box, 24, offset);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x111520), 0);
    lv_obj_set_style_radius(box, 4, 0);
    lv_obj_set_style_border_color(box, COLOR_CYAN, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_pad_all(box, 0, 0);

    lv_obj_t *title = create_label(parent, &font_inter_bold_18, COLOR_TEXT_1, "NO AIRCRAFT");
    lv_obj_set_pos(title, 48, offset + 26);
    lv_obj_t *message = create_label(parent, &font_inter_regular_14, COLOR_TEXT_2,
                                     "Waiting for the next ADS-B update");
    lv_obj_set_pos(message, 48, offset + 60);
}

enum class AircraftIconKind : uint8_t {
    Plane,
    Helicopter,
};

AircraftIconKind aircraft_icon_kind(const AircraftDetailsStruct &aircraft) {
    if (aircraft.category == "A7") return AircraftIconKind::Helicopter;

    String description = aircraft.description;
    description.toLowerCase();
    return description.indexOf("helicopter") >= 0 || description.indexOf("rotorcraft") >= 0
        ? AircraftIconKind::Helicopter
        : AircraftIconKind::Plane;
}

float aircraft_icon_bearing(const AircraftDetailsStruct &aircraft) {
    if (aircraft.hasTrack && isfinite(aircraft.track)) return aircraft.track;
    if (aircraft.hasHeading && isfinite(aircraft.heading)) return aircraft.heading;
    return 0.0f;
}

lv_point_t rotate_icon_point(int x, int y, int8_t offset_x, int8_t offset_y, float bearing) {
    const float angle = radians(bearing);
    const float cos_angle = cosf(angle);
    const float sin_angle = sinf(angle);
    return {
        (lv_coord_t)(x + offset_x * cos_angle - offset_y * sin_angle),
        (lv_coord_t)(y + offset_x * sin_angle + offset_y * cos_angle),
    };
}

void draw_aircraft_icon(lv_obj_t *canvas, int x, int y, const AircraftDetailsStruct &aircraft,
                        lv_color_t color, bool swept_wings = false) {
    const AircraftIconKind kind = aircraft_icon_kind(aircraft);
    const float bearing = aircraft_icon_bearing(aircraft);

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = color;
    line_dsc.width = 2;

    auto draw_segment = [&](int8_t x1, int8_t y1, int8_t x2, int8_t y2) {
        lv_point_t points[2] = {
            rotate_icon_point(x, y, x1, y1, bearing),
            rotate_icon_point(x, y, x2, y2, bearing),
        };
        lv_canvas_draw_line(canvas, points, 2, &line_dsc);
    };

    if (kind == AircraftIconKind::Helicopter) {
        draw_segment(-7, -7, 7, 7);  // rotor blade 1 (X)
        draw_segment(-7, 7, 7, -7);  // rotor blade 2 (X)
        draw_segment(0, -6, 0, 9);   // body
        line_dsc.width = 1;
        draw_segment(-3, 9, 3, 9);   // short tail
    } else {
        draw_segment(0, -10, 0, 7);    // fuselage
        draw_segment(0, -6, -7, 2); // left wing
        draw_segment(0, -5, -7, 2);    // left wing bac
        draw_segment(0, -6, 7, 2);  // right wing
        draw_segment(0, -5, 7, 2);     // right wing back

        line_dsc.width = 1;
        draw_segment(-3, 6, 3, 6);     // tailplane
    }
}

void render_map(lv_obj_t *parent, const FlightStats &stats) {
    lv_obj_t *canvas_holder = lv_obj_create(parent);
    lv_obj_remove_style_all(canvas_holder);
    lv_obj_set_size(canvas_holder, DISPLAY_WIDTH, DISPLAY_HEIGHT);

    static lv_color_t *canvas_buf = nullptr;
    if (!canvas_buf) {
        canvas_buf = (lv_color_t *)heap_caps_malloc(
            LV_CANVAS_BUF_SIZE_TRUE_COLOR(DISPLAY_WIDTH, DISPLAY_HEIGHT), MALLOC_CAP_SPIRAM);
    }
    if (!canvas_buf) return;

    lv_obj_t *canvas = lv_canvas_create(canvas_holder);
    lv_canvas_set_buffer(canvas, canvas_buf, DISPLAY_WIDTH, DISPLAY_HEIGHT, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(canvas, COLOR_BG, LV_OPA_COVER);

    const int centerX = DISPLAY_WIDTH / 2;
    const int centerY = DISPLAY_HEIGHT / 2;

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_hex(0x3a3a3a);
    line_dsc.width = 1;

    lv_draw_arc_dsc_t ring_dsc;
    lv_draw_arc_dsc_init(&ring_dsc);
    ring_dsc.color = lv_color_hex(0x3a3a3a);
    ring_dsc.width = 1;

    const float scale = 5.0f; // fixed scale, matches original implementation
    const float radius_10_miles = 10 * scale;
    const float radius_50_miles = 50 * scale;

    lv_canvas_draw_arc(canvas, centerX, centerY, (int16_t)radius_10_miles, 0, 360, &ring_dsc);
    lv_canvas_draw_arc(canvas, centerX, centerY, (int16_t)radius_50_miles, 0, 360, &ring_dsc);

    lv_point_t center_h[2] = {{(lv_coord_t)(centerX - 12), (lv_coord_t)centerY}, {(lv_coord_t)(centerX + 12), (lv_coord_t)centerY}};
    lv_canvas_draw_line(canvas, center_h, 2, &line_dsc);
    lv_point_t center_v[2] = {{(lv_coord_t)centerX, (lv_coord_t)(centerY - 12)}, {(lv_coord_t)centerX, (lv_coord_t)(centerY + 12)}};
    lv_canvas_draw_line(canvas, center_v, 2, &line_dsc);

    for (int i = 0; i < stats.totalAircraft; i++) {
        const AircraftDetailsStruct &ac = stats.aircraft[i];
        if (!isfinite(ac.latitude) || !isfinite(ac.longitude)) continue;

        float latDiffMiles = (ac.latitude - myLat) * 69.0f;
        float lonDiffMiles = (ac.longitude - myLon) * 69.0f * cos(radians(myLat));

        int x = centerX + (int)(lonDiffMiles * scale);
        int y = centerY - (int)(latDiffMiles * scale);
        constexpr int ICON_RADIUS = 10;
        if (x < ICON_RADIUS || x >= DISPLAY_WIDTH - ICON_RADIUS ||
            y < ICON_RADIUS || y >= DISPLAY_HEIGHT - ICON_RADIUS) continue;

        lv_color_t color = altitude_color(ac.altitude);
        draw_aircraft_icon(canvas, x, y, ac, color);
    }
}

// ── Radar screen (FlightScnr-style sweep display, adapted to a square panel)

// Range shown at the outer ring, in nautical miles — reads radarmap's
// runtime-configurable setting so the radar rings and background map always
// agree on scale (see radarmap::s_range_nmi, configurable via config.ini).
inline float radar_range_nmi() { return radarmap::s_range_nmi; }
constexpr int RADAR_RING_COUNT = 3;      // rings at RANGE/3, 2*RANGE/3, RANGE
constexpr uint32_t RADAR_SWEEP_PERIOD_MS = 4000; // one full rotation
constexpr uint32_t RADAR_SWEEP_TICK_MS = 60;

lv_color_t   *s_radar_canvas_buf = nullptr;
lv_obj_t     *s_radar_canvas_holder = nullptr;
lv_obj_t     *s_radar_canvas = nullptr;
lv_obj_t     *s_radar_sweep_canvas = nullptr; // transparent overlay, redrawn fast
lv_color_t   *s_radar_sweep_buf = nullptr;
lv_timer_t   *s_radar_sweep_timer = nullptr;

struct RadarHitTarget {
    int x;
    int y;
    char identifier[16];
    char description[48];
    char route[64];
    char status[16];
    int altitude;
    int squawk;
    float distance;
    float speed;
};

RadarHitTarget s_radar_hit_targets[100];
uint8_t s_radar_hit_target_count = 0;
constexpr int RADAR_HIT_RADIUS_PX = 18;

// Largest square that fits inside the round-display-derived layout while
// leaving room for the N/S/E/W labels at top/bottom/left/right.
int radar_center_x() { return DISPLAY_WIDTH / 2; }
int radar_center_y() { return DISPLAY_HEIGHT / 2; }
int radar_outer_radius() { return (DISPLAY_WIDTH < DISPLAY_HEIGHT ? DISPLAY_WIDTH : DISPLAY_HEIGHT) / 2 - 30; }

void dismiss_radar_info() {
    if (s_radar_info_timer) {
        lv_timer_del(s_radar_info_timer);
        s_radar_info_timer = nullptr;
    }
    if (s_radar_info_box) {
        lv_obj_del(s_radar_info_box);
        s_radar_info_box = nullptr;
    }
}

void radar_info_timeout_cb(lv_timer_t *timer) {
    (void)timer;
    dismiss_radar_info();
}

void show_radar_info(const RadarHitTarget &target, bool persistent = false) {
    dismiss_radar_info();

    s_radar_info_box = lv_obj_create(s_root);
    lv_obj_set_size(s_radar_info_box, DISPLAY_WIDTH - 40, 86);
    lv_obj_set_pos(s_radar_info_box, 20, DISPLAY_HEIGHT - 132);
    lv_obj_set_style_bg_color(s_radar_info_box, lv_color_hex(0x111520), 0);
    lv_obj_set_style_bg_opa(s_radar_info_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(s_radar_info_box, COLOR_CYAN, 0);
    lv_obj_set_style_border_width(s_radar_info_box, 1, 0);
    lv_obj_set_style_radius(s_radar_info_box, 8, 0);
    lv_obj_set_style_pad_all(s_radar_info_box, 0, 0);
    lv_obj_clear_flag(s_radar_info_box, LV_OBJ_FLAG_SCROLLABLE);

    String title = String(target.identifier) + "  " + target.description;
    lv_obj_t *title_label = create_label(s_radar_info_box, &font_inter_bold_16, COLOR_TEXT_1, title.c_str());
    lv_obj_set_width(title_label, DISPLAY_WIDTH - 64);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(title_label, 12, 8);

    char metrics[96];
    snprintf(metrics, sizeof(metrics), "%d FT   %d KT   %.1f NMI   SQWK %04d",
             target.altitude, (int)target.speed, target.distance, target.squawk);
    lv_obj_t *metrics_label = create_label(s_radar_info_box, &font_jetbrainsmono_medium_12, COLOR_CYAN, metrics);
    lv_obj_set_pos(metrics_label, 12, 34);

    String routeStr = String(target.route);
    String details = (routeStr.length() > 0 && routeStr != "Unknown")
        ? String(target.status) + "  " + routeStr
        : String(target.status);
    lv_obj_t *details_label = create_label(s_radar_info_box, &font_inter_regular_12, COLOR_TEXT_2, details.c_str());
    lv_obj_set_width(details_label, DISPLAY_WIDTH - 64);
    lv_label_set_long_mode(details_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(details_label, 12, 58);

    lv_obj_move_foreground(s_radar_info_box);
    if (!persistent) {
        s_radar_info_timer = lv_timer_create(radar_info_timeout_cb, 5000, nullptr);
        lv_timer_set_repeat_count(s_radar_info_timer, 1);
    }
}

void radar_show_nearest_info(const FlightStats &stats) {
    if (stats.totalAircraft <= 0 || stats.closestAircraft >= stats.totalAircraft) {
        dismiss_radar_info();
        return;
    }

    const AircraftDetailsStruct &ac = stats.aircraft[stats.closestAircraft];
    RadarHitTarget target{};
    snprintf(target.identifier, sizeof(target.identifier), "%s", ac.identifier.c_str());
    snprintf(target.description, sizeof(target.description), "%s", ac.description.c_str());
    snprintf(target.route, sizeof(target.route), "%s", ac.route.c_str());
    snprintf(target.status, sizeof(target.status), "%s", ac.status.c_str());
    target.altitude = ac.altitude;
    target.squawk = ac.squawk;
    target.distance = ac.distance;
    target.speed = ac.speed;

    show_radar_info(target, true);
}

void radar_canvas_click_cb(lv_event_t *event) {
    lv_indev_t *indev = lv_event_get_indev(event);
    if (!indev) return;

    lv_point_t touch;
    lv_indev_get_point(indev, &touch);
    int closest_index = -1;
    int closest_distance_sq = RADAR_HIT_RADIUS_PX * RADAR_HIT_RADIUS_PX;
    for (uint8_t i = 0; i < s_radar_hit_target_count; i++) {
        int delta_x = touch.x - s_radar_hit_targets[i].x;
        int delta_y = touch.y - s_radar_hit_targets[i].y;
        int distance_sq = delta_x * delta_x + delta_y * delta_y;
        if (distance_sq <= closest_distance_sq) {
            closest_distance_sq = distance_sq;
            closest_index = i;
        }
    }
    if (closest_index >= 0) show_radar_info(s_radar_hit_targets[closest_index]);
}

void radar_draw_static(lv_obj_t *canvas) {
    const int cx = radar_center_x();
    const int cy = radar_center_y();
    const int outerR = radar_outer_radius();

    lv_draw_arc_dsc_t ring_dsc;
    lv_draw_arc_dsc_init(&ring_dsc);
    ring_dsc.color = lv_color_hex(0x3d523d);
    ring_dsc.width = 1;

    for (int i = 1; i <= RADAR_RING_COUNT; i++) {
        int r = outerR * i / RADAR_RING_COUNT;
        lv_canvas_draw_arc(canvas, cx, cy, r, 0, 360, &ring_dsc);
    }

    lv_draw_line_dsc_t cross_dsc;
    lv_draw_line_dsc_init(&cross_dsc);
    cross_dsc.color = lv_color_hex(0x3d523d);
    cross_dsc.width = 1;

    lv_point_t horiz[2] = {{(lv_coord_t)(cx - outerR), (lv_coord_t)cy}, {(lv_coord_t)(cx + outerR), (lv_coord_t)cy}};
    lv_canvas_draw_line(canvas, horiz, 2, &cross_dsc);
    lv_point_t vert[2] = {{(lv_coord_t)cx, (lv_coord_t)(cy - outerR)}, {(lv_coord_t)cx, (lv_coord_t)(cy + outerR)}};
    lv_canvas_draw_line(canvas, vert, 2, &cross_dsc);
}

void radar_draw_aircraft(lv_obj_t *canvas, const FlightStats &stats) {
    const int cx = radar_center_x();
    const int cy = radar_center_y();
    const int outerR = radar_outer_radius();

    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.font = &font_inter_regular_14;

    const float rangeNmi = radar_range_nmi();

    s_radar_hit_target_count = 0;
    for (int i = 0; i < stats.totalAircraft; i++) {
        const AircraftDetailsStruct &ac = stats.aircraft[i];
        if (!isfinite(ac.distance) || !isfinite(ac.latitude) || !isfinite(ac.longitude) ||
            ac.distance <= 0 || ac.distance > rangeNmi) continue;

        float latDiffMiles = (ac.latitude - myLat) * 69.0f;
        float lonDiffMiles = (ac.longitude - myLon) * 69.0f * cos(radians(myLat));
        float bearing = atan2(lonDiffMiles, latDiffMiles); // radians, 0 = north
        if (!isfinite(bearing)) continue;

        float r = (ac.distance / rangeNmi) * outerR;
        int x = cx + (int)(r * sin(bearing));
        int y = cy - (int)(r * cos(bearing));

        constexpr int ICON_RADIUS = 10;
        if (x < ICON_RADIUS || x >= DISPLAY_WIDTH - ICON_RADIUS ||
            y < ICON_RADIUS || y >= DISPLAY_HEIGHT - ICON_RADIUS) continue;

        bool isClosest = radarmap::s_always_show_nearest_info && i == stats.closestAircraft;
        if (isClosest) {
            lv_draw_arc_dsc_t highlight_dsc;
            lv_draw_arc_dsc_init(&highlight_dsc);
            highlight_dsc.color = COLOR_CYAN;
            highlight_dsc.width = 2;
            lv_canvas_draw_arc(canvas, x - 1, y, 16, 0, 360, &highlight_dsc);
        }

        lv_color_t color = altitude_color(ac.altitude);
        draw_aircraft_icon(canvas, x, y, ac, color, true);

        if (s_radar_hit_target_count < sizeof(s_radar_hit_targets) / sizeof(s_radar_hit_targets[0])) {
            RadarHitTarget &target = s_radar_hit_targets[s_radar_hit_target_count++];
            target.x = x;
            target.y = y;
            snprintf(target.identifier, sizeof(target.identifier), "%s", ac.identifier.c_str());
            snprintf(target.description, sizeof(target.description), "%s", ac.description.c_str());
            snprintf(target.route, sizeof(target.route), "%s", ac.route.c_str());
            snprintf(target.status, sizeof(target.status), "%s", ac.status.c_str());
            target.altitude = ac.altitude;
            target.squawk = ac.squawk;
            target.distance = ac.distance;
            target.speed = ac.speed;
        }

        label_dsc.color = color;
        char idBuf[12];
        snprintf(idBuf, sizeof(idBuf), "%s", ac.identifier.c_str());
        int labelX = isClosest ? x + 17 : x + 8;
        lv_area_t label_area = {(lv_coord_t)labelX, (lv_coord_t)(y - 8), (lv_coord_t)(labelX + 92), (lv_coord_t)(y + 8)};
        lv_canvas_draw_text(canvas, label_area.x1, label_area.y1, 92, &label_dsc, idBuf);
    }
}

void radar_sweep_timer_cb(lv_timer_t *timer) {
    if (!s_radar_sweep_canvas || !s_radar_sweep_buf) return;

    lv_canvas_fill_bg(s_radar_sweep_canvas, COLOR_BG, LV_OPA_TRANSP);

    const int cx = radar_center_x();
    const int cy = radar_center_y();
    const int outerR = radar_outer_radius();

    uint32_t phase = millis() % RADAR_SWEEP_PERIOD_MS;
    float angle = (float)phase / RADAR_SWEEP_PERIOD_MS * 360.0f;
    float rad = radians(angle);

    lv_draw_line_dsc_t sweep_dsc;
    lv_draw_line_dsc_init(&sweep_dsc);
    sweep_dsc.color = COLOR_GREEN;
    sweep_dsc.width = 2;
    sweep_dsc.opa = LV_OPA_COVER;

    int x = cx + (int)(outerR * sin(rad));
    int y = cy - (int)(outerR * cos(rad));
    lv_point_t sweep_line[2] = {{(lv_coord_t)cx, (lv_coord_t)cy}, {(lv_coord_t)x, (lv_coord_t)y}};
    lv_canvas_draw_line(s_radar_sweep_canvas, sweep_line, 2, &sweep_dsc);

    lv_draw_line_dsc_t trail_dsc;
    lv_draw_line_dsc_init(&trail_dsc);
    trail_dsc.color = COLOR_GREEN;
    trail_dsc.width = 1;
    for (int t = 1; t <= 18; t++) {
        float trailAngle = angle - t * 1.5f;
        float trailRad = radians(trailAngle);
        trail_dsc.opa = LV_OPA_COVER - (LV_OPA_COVER * t / 18);
        int tx = cx + (int)(outerR * sin(trailRad));
        int ty = cy - (int)(outerR * cos(trailRad));
        lv_point_t trail_line[2] = {{(lv_coord_t)cx, (lv_coord_t)cy}, {(lv_coord_t)tx, (lv_coord_t)ty}};
        lv_canvas_draw_line(s_radar_sweep_canvas, trail_line, 2, &trail_dsc);
    }
}

void render_radar(lv_obj_t *parent, const FlightStats &stats) {
    s_radar_canvas_holder = lv_obj_create(parent);
    lv_obj_remove_style_all(s_radar_canvas_holder);
    lv_obj_set_size(s_radar_canvas_holder, DISPLAY_WIDTH, DISPLAY_HEIGHT);

    if (!s_radar_canvas_buf) {
        s_radar_canvas_buf = (lv_color_t *)heap_caps_malloc(
            LV_CANVAS_BUF_SIZE_TRUE_COLOR(DISPLAY_WIDTH, DISPLAY_HEIGHT), MALLOC_CAP_SPIRAM);
    }
    if (!s_radar_canvas_buf) return;

    s_radar_canvas = lv_canvas_create(s_radar_canvas_holder);
    lv_canvas_set_buffer(s_radar_canvas, s_radar_canvas_buf, DISPLAY_WIDTH, DISPLAY_HEIGHT, LV_IMG_CF_TRUE_COLOR);
    lv_obj_add_flag(s_radar_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_radar_canvas, radar_canvas_click_cb, LV_EVENT_CLICKED, nullptr);

    if (radarmap::ready() && radarmap::MAP_SIZE_PX == DISPLAY_WIDTH && radarmap::MAP_SIZE_PX == DISPLAY_HEIGHT) {
        memcpy(s_radar_canvas_buf, radarmap::buffer(),
               (size_t)DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(lv_color_t));
    } else {
        lv_canvas_fill_bg(s_radar_canvas, COLOR_BG, LV_OPA_COVER);
    }

    radar_draw_static(s_radar_canvas);
    radar_draw_aircraft(s_radar_canvas, stats);

    if (radarmap::s_always_show_nearest_info) {
        radar_show_nearest_info(stats);
    }

    if (radarmap::s_scan_line_enabled && !s_radar_sweep_buf) {
        s_radar_sweep_buf = (lv_color_t *)heap_caps_malloc(
            LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(DISPLAY_WIDTH, DISPLAY_HEIGHT), MALLOC_CAP_SPIRAM);
    }
    if (radarmap::s_scan_line_enabled && s_radar_sweep_buf) {
        s_radar_sweep_canvas = lv_canvas_create(s_radar_canvas_holder);
        lv_canvas_set_buffer(s_radar_sweep_canvas, s_radar_sweep_buf, DISPLAY_WIDTH, DISPLAY_HEIGHT, LV_IMG_CF_TRUE_COLOR_ALPHA);
        lv_canvas_fill_bg(s_radar_sweep_canvas, COLOR_BG, LV_OPA_TRANSP);
    }

    const int cx = radar_center_x();
    const int cy = radar_center_y();
    const int outerR = radar_outer_radius();

    lv_obj_t *lblN = create_label(s_radar_canvas_holder, &font_inter_bold_16, COLOR_GREEN, "N");
    lv_obj_align(lblN, LV_ALIGN_TOP_LEFT, cx - 5, cy - outerR - 22);
    lv_obj_t *lblS = create_label(s_radar_canvas_holder, &font_inter_bold_16, COLOR_GREEN, "S");
    lv_obj_align(lblS, LV_ALIGN_TOP_LEFT, cx - 5, cy + outerR + 4);
    lv_obj_t *lblE = create_label(s_radar_canvas_holder, &font_inter_bold_16, COLOR_GREEN, "E");
    lv_obj_align(lblE, LV_ALIGN_TOP_LEFT, cx + outerR + 8, cy - 8);
    lv_obj_t *lblW = create_label(s_radar_canvas_holder, &font_inter_bold_16, COLOR_GREEN, "W");
    lv_obj_align(lblW, LV_ALIGN_TOP_LEFT, cx - outerR - 20, cy - 8);

    char rangeBuf[16];
    snprintf(rangeBuf, sizeof(rangeBuf), "%dnmi", (int)radar_range_nmi());
    lv_obj_t *lblRange = create_label(s_radar_canvas_holder, &font_inter_regular_14, COLOR_TEXT_3, rangeBuf);
    lv_obj_align(lblRange, LV_ALIGN_TOP_LEFT, cx + 6, cy - outerR + 2);

    if (radarmap::s_scan_line_enabled && !s_radar_sweep_timer) {
        s_radar_sweep_timer = lv_timer_create(radar_sweep_timer_cb, RADAR_SWEEP_TICK_MS, nullptr);
    }
    if (radarmap::s_scan_line_enabled && s_radar_sweep_timer) {
        lv_timer_resume(s_radar_sweep_timer);
    }
}

void radar_stop() {
    if (s_radar_sweep_timer) {
        lv_timer_pause(s_radar_sweep_timer);
    }
    s_radar_canvas_holder = nullptr;
    s_radar_canvas = nullptr;
    s_radar_sweep_canvas = nullptr;
}

} // namespace

// ── Public API ─────────────────────────────────────────────────────────────

bool display_ready() {
    return s_display_ready;
}

void display_debug_line(const String &line) {
    Serial.println(line);
    if (!s_display_ready || !s_startup_active) return;

    for (uint8_t i = 0; i < STARTUP_LOG_LINES - 1; i++) {
        s_startup_lines[i] = s_startup_lines[i + 1];
    }
    s_startup_lines[STARTUP_LOG_LINES - 1] = line;

    if (!s_startup_log) {
        s_startup_log = lv_obj_create(s_root);
        lv_obj_remove_style_all(s_startup_log);
        lv_obj_set_size(s_startup_log, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        lv_obj_clear_flag(s_startup_log, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_move_background(s_startup_log);

        lv_obj_t *title = create_label(s_startup_log, &font_inter_bold_18, COLOR_TEXT_1, "ADSB MONITOR");
        lv_obj_set_pos(title, 20, 24);
        lv_obj_t *subtitle = create_label(s_startup_log, &font_inter_regular_14, COLOR_TEXT_2, "Starting up");
        lv_obj_set_pos(subtitle, 20, 52);
    }

    String text;
    for (uint8_t i = 0; i < STARTUP_LOG_LINES; i++) {
        if (!s_startup_lines[i].isEmpty()) {
            if (!text.isEmpty()) text += '\n';
            text += s_startup_lines[i];
        }
    }

    lv_obj_t *log = lv_obj_get_child(s_startup_log, 2);
    if (!log) {
        log = create_label(s_startup_log, &font_jetbrainsmono_medium_12, COLOR_TEXT_2, "");
        lv_obj_set_width(log, DISPLAY_WIDTH - 40);
        lv_label_set_long_mode(log, LV_LABEL_LONG_WRAP);
        lv_obj_set_pos(log, 20, 92);
    }
    lv_label_set_text(log, text.c_str());
    lv_refr_now(nullptr);
}

void display_init() {
    Serial.println("display_init: start");
    Serial.printf("SRAM free: %d  PSRAM free: %d\n",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    // 1. Touch first (matches working reference)
    Wire.begin(TOUCH_SDA, TOUCH_SCL);
    s_ts.begin();
    s_ts.setRotation(ROTATION_NORMAL);

    // 2. Display panel
    s_gfx->begin(11000000);
    s_gfx->fillScreen(RGB565_BLACK);

    // 3. LVGL init (backlight stays OFF until after first flush below)
    lv_init();

    uint32_t screenWidth  = s_gfx->width();
    uint32_t screenHeight = s_gfx->height();

    lv_color_t *buf = (lv_color_t *)heap_caps_malloc(
        sizeof(lv_color_t) * screenWidth * screenHeight,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (!buf) {
        Serial.println("display_init: LVGL draw buffer alloc failed!");
        return;
    }

    lv_disp_draw_buf_init(&s_draw_buf, buf, nullptr, screenWidth * screenHeight);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res  = screenWidth;
    s_disp_drv.ver_res  = screenHeight;
    s_disp_drv.flush_cb = lvgl_flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&s_disp_drv);

    s_root = lv_scr_act();
    lv_obj_set_style_bg_color(s_root, COLOR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_timer_handler();

    // Backlight ON at full brightness
    ledcAttach(GFX_BL, 5000, 8);
    ledcWrite(GFX_BL, 255);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type         = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb      = lvgl_touch_cb;
    indev_drv.scroll_throw = 20;
    indev_drv.scroll_limit = 20;
    lv_indev_drv_register(&indev_drv);

    create_touch_zones();

    s_clock_label = create_label(s_root, &font_inter_bold_18, COLOR_CLOCK, "--:--:--");
    lv_obj_add_flag(s_clock_label, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(s_clock_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(s_clock_label, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_move_foreground(s_clock_label);

    s_display_ready = true;
    Serial.println("display_init: done");
    Serial.printf("SRAM free: %d  PSRAM free: %d\n",
                  heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

void display_set_brightness(uint8_t level) {
    ledcWrite(GFX_BL, level > 0 ? 255 : 0);
}

void display_render_frame(uint8_t frame, const FlightStats &stats) {
    lv_obj_t *parent = begin_content(frame == FRAME_RADAR);

    if (stats.totalAircraft == 0) {
        render_empty(parent);
        return;
    }

    switch (frame) {
    case FRAME_SYSINFO:
        render_sysinfo(parent, stats);
        break;
    case FRAME_OVERVIEW:
        render_aircraft_card(parent, stats, stats.aircraft[stats.closestAircraft], false);
        break;
    case FRAME_TOPSTATS:
        render_topstats(parent, stats);
        break;
    case FRAME_MAP:
        render_map(parent, stats);
        break;
    case FRAME_RADAR:
        render_radar(parent, stats);
        break;
    default: {
        int emergencyIndex = frame - FRAME_EMERGENCY_BASE;
        if (emergencyIndex >= 0 && emergencyIndex < stats.emergencyCount && emergencyIndex < 4) {
            render_aircraft_card(parent, stats, stats.aircraft[stats.emergencyAircraft[emergencyIndex]], true);
        }
        break;
    }
    }

    if (frame == FRAME_RADAR) {
        lv_obj_add_flag(s_clock_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_clock_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_clock_label);
    }
}

void display_update_clock(const char *time_text) {
    if (!s_clock_label || !time_text) return;
    lv_label_set_text(s_clock_label, time_text);
}
