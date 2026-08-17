#pragma once
#include <stdint.h>
#include <WiFi.h>

// merlinFlightStats.h expects these DEBUG_PRINT* macros to already be defined
// (normally supplied by merlinNetwork.h, included first in main.cpp).
#ifndef DEBUG_PRINTLN
#ifdef DEBUG
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTDEC(x, DEC) Serial.print(x, DEC)
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINTLNDEC(x, DEC) Serial.println(x, DEC)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTDEC(x, DEC)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINTLNDEC(x, DEC)
#endif
#endif

#include "merlinFlightStats.h"

constexpr int DISPLAY_WIDTH = 480;
constexpr int DISPLAY_HEIGHT = 480;

// UI "frames" cycled through on the touch-driven carousel.
enum DisplayFrame : uint8_t {
    FRAME_SYSINFO = 0,
    FRAME_OVERVIEW = 1,
    FRAME_TOPSTATS = 2,
    FRAME_MAP = 3,
    FRAME_RADAR = 4,
    FRAME_EMERGENCY_BASE = 5, // FRAME_EMERGENCY_BASE..+3 for up to 4 emergency aircraft
};

// Flags to enable/disable individual carousel screens. Set to false to skip
// a screen when advancing through the frame sequence in main.cpp.
constexpr bool FRAME_SYSINFO_ENABLED = true;
constexpr bool FRAME_OVERVIEW_ENABLED = true;
constexpr bool FRAME_TOPSTATS_ENABLED = true;
constexpr bool FRAME_MAP_ENABLED = false;
constexpr bool FRAME_RADAR_ENABLED = true;

// While FRAME_RADAR is active, poll ADS-B data faster so aircraft motion is
// visible on the sweep. Restored to the normal interval on any other frame.
constexpr unsigned long UPDATE_ADSBS_INTERVAL_RADAR_MILLISECS = 5000;

void display_init();
bool display_ready();
void display_set_brightness(uint8_t level);
void display_render_frame(uint8_t frame, const FlightStats &stats);
void display_update_clock(const char *time_text);
void display_debug_line(const String &line);
void display_toggle_info_overlay();

// Touch tap-zone callbacks, invoked from the LVGL event handlers registered
// inside display.cpp. Implemented in main.cpp.
void onTouchAdvanceFrame();
void onTouchRotateBrightness();
void onTouchTriggerFetch();
void onTouchToggleSysInfo();
void onTouchToggleInfoOverlay();
void onTouchReboot();
