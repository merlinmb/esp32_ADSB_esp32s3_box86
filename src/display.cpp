#include "display.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <TAMC_GT911.h>
#include <WiFi.h>
#include <lvgl.h>
#include <TimeLib.h>
#include <stdio.h>
#include <string.h>
#include <esp_heap_caps.h>

// Corrected ST7701 init: 0x20 = INVOFF (not 0x21 IPS inversion), 0x50 = RGB565 (not 0x60 RGB666)
static const uint8_t st7701_init_corrected[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x10,

    WRITE_C8_D16, 0xC0, 0x3B, 0x00,
    WRITE_C8_D16, 0xC1, 0x0D, 0x02,
    WRITE_C8_D16, 0xC2, 0x31, 0x05,
    WRITE_C8_D8, 0xCD, 0x08,

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

    WRITE_COMMAND_8, 0x20,   // INVOFF — was 0x21 (IPS inversion), causing all colours to be inverted
    WRITE_C8_D8, 0x3A, 0x50, // RGB565 — was 0x60 (RGB666), mismatched with 16-bit framebuffer

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
    0  /* pclk_active_neg */, 12000000 /* prefer_speed */);

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

static void lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    static bool          s_was_touched  = false;
    static unsigned long s_touch_down_ms = 0;
    static constexpr unsigned long DEBOUNCE_MS = 50;

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
        onTouchTriggerFetch();
    } else {
        s_last_left_tap_ms = now_ms;
        onTouchAdvanceFrame();
    }
}

void right_zone_click_cb(lv_event_t *e) {
    unsigned long now_ms = millis();
    if (now_ms - s_last_right_tap_ms <= DOUBLE_TAP_WINDOW_MS) {
        s_last_right_tap_ms = 0;
        onTouchToggleSysInfo();
    } else {
        s_last_right_tap_ms = now_ms;
        onTouchRotateBrightness();
    }
}

void right_zone_long_press_cb(lv_event_t *e) {
    onTouchReboot();
}

void create_touch_zones() {
    s_touch_zone_left = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_touch_zone_left);
    lv_obj_set_size(s_touch_zone_left, DISPLAY_WIDTH / 2, DISPLAY_HEIGHT);
    lv_obj_align(s_touch_zone_left, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_flag(s_touch_zone_left, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_touch_zone_left, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_touch_zone_left, left_zone_click_cb, LV_EVENT_CLICKED, nullptr);

    s_touch_zone_right = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_touch_zone_right);
    lv_obj_set_size(s_touch_zone_right, DISPLAY_WIDTH / 2, DISPLAY_HEIGHT);
    lv_obj_align(s_touch_zone_right, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_flag(s_touch_zone_right, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_touch_zone_right, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_touch_zone_right, right_zone_click_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(s_touch_zone_right, right_zone_long_press_cb, LV_EVENT_LONG_PRESSED, nullptr);
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

void clear_content() {
    if (s_content) {
        lv_obj_del(s_content);
        s_content = nullptr;
    }
}

lv_obj_t *begin_content() {
    clear_content();
    s_content = lv_obj_create(s_root);
    lv_obj_remove_style_all(s_content);
    lv_obj_set_size(s_content, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_clear_flag(s_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_background(s_content); // keep touch zones on top for input
    return s_content;
}

void row_label(lv_obj_t *parent, int y, const char *label, const char *value, lv_color_t value_color) {
    lv_obj_t *l = create_label(parent, &lv_font_montserrat_16, COLOR_TEXT_2, label);
    lv_obj_set_pos(l, 20, y);
    lv_obj_t *v = create_label(parent, &lv_font_montserrat_16, value_color, value);
    lv_obj_set_pos(v, 200, y);
}

void render_sysinfo(lv_obj_t *parent, const FlightStats &stats) {
    (void)stats;
    create_label(parent, &lv_font_montserrat_20, COLOR_CYAN, "Aircraft Tracker");

    float batteryVoltage = (analogRead(4) * 2 * 3.3f * 1000) / 4096;
    char battStr[24];
    snprintf(battStr, sizeof(battStr), "%.1fV %.0f%%", batteryVoltage / 1000, batteryVoltage / 1000 / 5.0f * 100);

    char rssiStr[8];
    snprintf(rssiStr, sizeof(rssiStr), "%d%%", (int)WiFi.RSSI());
    char heapStr[16];
    snprintf(heapStr, sizeof(heapStr), "%u", (unsigned)ESP.getFreeHeap());

    row_label(parent, 60, "WiFi", WiFi.SSID().c_str(), COLOR_TEXT_1);
    row_label(parent, 90, "IP", WiFi.localIP().toString().c_str(), COLOR_TEXT_1);
    row_label(parent, 120, "Signal", rssiStr, COLOR_TEXT_1);
    row_label(parent, 150, "Battery", battStr, COLOR_TEXT_1);
    row_label(parent, 180, "Free heap", heapStr, COLOR_TEXT_1);
    row_label(parent, 210, "By", "markbeets@gmail.com", COLOR_TEXT_3);
}

void render_topstats(lv_obj_t *parent, const FlightStats &stats) {
    create_label(parent, &lv_font_montserrat_20, COLOR_TEXT_1, "Aircraft Statistics");

    const AircraftDetailsStruct &fastest  = stats.aircraft[stats.fastestAircraft];
    const AircraftDetailsStruct &slowest  = stats.aircraft[stats.slowestAircraft];
    const AircraftDetailsStruct &highest  = stats.aircraft[stats.highestAircraft];
    const AircraftDetailsStruct &lowest   = stats.aircraft[stats.lowestAircraft];
    const AircraftDetailsStruct &closest  = stats.aircraft[stats.closestAircraft];
    const AircraftDetailsStruct &farthest = stats.aircraft[stats.farthestAircraft];

    row_label(parent, 60,  "Fastest",  (fastest.identifier + " " + String((int)fastest.speed) + "kt").c_str(), COLOR_TEXT_1);
    row_label(parent, 90,  "Slowest",  (slowest.identifier + " " + String((int)slowest.speed) + "kt").c_str(), COLOR_TEXT_1);
    row_label(parent, 120, "Highest",  (highest.identifier + " " + String((int)highest.altitude) + "ft").c_str(), COLOR_TEXT_1);
    row_label(parent, 150, "Lowest",   (lowest.identifier + " " + String((int)lowest.altitude) + "ft").c_str(), COLOR_TEXT_1);
    row_label(parent, 180, "Closest",  (closest.identifier + " " + String((int)closest.distance) + "nmi").c_str(), COLOR_TEXT_1);
    row_label(parent, 210, "Farthest", (farthest.identifier + " " + String((int)farthest.distance) + "nmi").c_str(), COLOR_TEXT_1);
    row_label(parent, 240, "Emergencies",
              stats.emergencyCount > 0 ? String(stats.emergencyCount).c_str() : "None",
              stats.emergencyCount > 0 ? COLOR_RED : COLOR_GREEN);
}

void render_aircraft_card(lv_obj_t *parent, const FlightStats &stats, const AircraftDetailsStruct &aircraft, bool is_emergency) {
    if (is_emergency) {
        lv_obj_set_style_border_color(parent, COLOR_RED, 0);
        lv_obj_set_style_border_width(parent, 4, 0);
    }

    create_label(parent, &lv_font_montserrat_14, COLOR_TEXT_2, "closest flight");

    if (stats.totalAircraft > 0) {
        lv_obj_t *badge = lv_obj_create(parent);
        lv_obj_set_size(badge, 70, 40);
        lv_obj_set_pos(badge, DISPLAY_WIDTH - 90, 20);
        lv_obj_set_style_bg_color(badge, COLOR_MAGENTA, 0);
        lv_obj_set_style_radius(badge, 6, 0);
        lv_obj_t *count = create_label(badge, &lv_font_montserrat_20, lv_color_black(), String(stats.totalAircraft).c_str());
        lv_obj_center(count);
    }

    if (aircraft.identifier.length() > 0) {
        lv_obj_t *l = create_label(parent, &lv_font_montserrat_20, COLOR_TEXT_1, aircraft.identifier.c_str());
        lv_obj_set_pos(l, 20, 60);
    }

    if (aircraft.description.length() > 0) {
        lv_obj_t *l = create_label(parent, &lv_font_montserrat_18, COLOR_TEXT_1, aircraft.description.c_str());
        lv_obj_set_pos(l, 20, 110);
    }

    row_label(parent, 160, "distance away", (String((int)aircraft.distance) + "nmi").c_str(), COLOR_TEXT_1);
    row_label(parent, 190, "status", aircraft.status.c_str(), status_color(aircraft.status));
    row_label(parent, 220, "squawk", String(aircraft.squawk).c_str(),
              isSquawkEmergency(aircraft.squawk) ? COLOR_RED : COLOR_TEXT_1);
    row_label(parent, 250, "altitude", (String((int)aircraft.altitude) + "ft").c_str(), COLOR_TEXT_1);
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
    lv_obj_set_size(box, DISPLAY_WIDTH, 70);
    lv_obj_set_pos(box, 0, offset);
    lv_obj_set_style_bg_color(box, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_radius(box, 5, 0);

    lv_obj_t *l1 = create_label(box, &lv_font_montserrat_20, lv_color_black(), "No aircraft are");
    lv_obj_align(l1, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_t *l2 = create_label(box, &lv_font_montserrat_20, lv_color_black(), "currently being tracked");
    lv_obj_align(l2, LV_ALIGN_TOP_MID, 0, 34);
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

    lv_draw_rect_dsc_t dot_dsc;
    lv_draw_rect_dsc_init(&dot_dsc);
    dot_dsc.radius = LV_RADIUS_CIRCLE;

    for (int i = 0; i < stats.totalAircraft; i++) {
        const AircraftDetailsStruct &ac = stats.aircraft[i];
        float latDiffMiles = (ac.latitude - myLat) * 69.0f;
        float lonDiffMiles = (ac.longitude - myLon) * 69.0f * cos(radians(myLat));

        int x = centerX + (int)(lonDiffMiles * scale);
        int y = centerY - (int)(latDiffMiles * scale);
        if (x < 0 || x >= DISPLAY_WIDTH || y < 0 || y >= DISPLAY_HEIGHT) continue;

        lv_color_t color = altitude_color(ac.altitude);
        dot_dsc.bg_color = color;
        lv_area_t dot_area = {(lv_coord_t)(x - 8), (lv_coord_t)(y - 8), (lv_coord_t)(x + 8), (lv_coord_t)(y + 8)};
        lv_canvas_draw_rect(canvas, dot_area.x1, dot_area.y1, 16, 16, &dot_dsc);

        float headingRadians = radians(ac.heading);
        int lineX = x + (int)(25 * sin(headingRadians));
        int lineY = y - (int)(25 * cos(headingRadians));
        line_dsc.color = color;
        lv_point_t heading_line[2] = {{(lv_coord_t)x, (lv_coord_t)y}, {(lv_coord_t)lineX, (lv_coord_t)lineY}};
        lv_canvas_draw_line(canvas, heading_line, 2, &line_dsc);
    }
}

} // namespace

// ── Public API ─────────────────────────────────────────────────────────────

bool display_ready() {
    return s_display_ready;
}

void display_debug_line(const String &line) {
    Serial.println(line);
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

    s_clock_label = create_label(s_root, &lv_font_montserrat_20, COLOR_CLOCK, "--:--:--");
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
    ledcWrite(GFX_BL, level);
}

void display_render_frame(uint8_t frame, const FlightStats &stats) {
    lv_obj_t *parent = begin_content();

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
    default: {
        int emergencyIndex = frame - FRAME_EMERGENCY_BASE;
        if (emergencyIndex >= 0 && emergencyIndex < stats.emergencyCount && emergencyIndex < 4) {
            render_aircraft_card(parent, stats, stats.aircraft[stats.emergencyAircraft[emergencyIndex]], true);
        }
        break;
    }
    }

    lv_obj_move_foreground(s_clock_label);
}

void display_update_clock() {
    if (!s_clock_label) return;
    char buf[10];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hour(), minute(), second());
    lv_label_set_text(s_clock_label, buf);
}
