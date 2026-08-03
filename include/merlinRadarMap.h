#pragma once

// Background basemap for the radar screen — a static export from one of
// Esri's free basemap services, centered on myLat/myLon and sized to exactly
// match the radar's range so roads line up with real distances. Style and
// range are runtime-configurable (config.ini / device webserver — see
// s_enabled, s_style, s_range_nmi below).
//
// Fetched once (HTTPS GET, no API key required) and cached to SPIFFS as
// /radarmap.jpg so subsequent boots don't need network access. Decoded with
// TJpgDec (already vendored inside LVGL's sjpg extra lib, enabled via
// LV_USE_SJPG in lv_conf.h) straight into a persistent RGB565 PSRAM buffer —
// bypassing LVGL's lv_img/lv_sjpg wrapper, which isn't a fit for a single
// static 480x480 background layer.

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <lvgl.h>

extern "C" {
#include "src/extra/libs/sjpg/tjpgd.h"
}

#include "merlinFlightStats.h" // myLat, myLon

namespace radarmap {

constexpr int MAP_SIZE_PX = 480; // matches DISPLAY_WIDTH/HEIGHT

// The radar's outer ring is drawn at radar_outer_radius() (display.cpp),
// which is inset from the canvas edge to leave room for N/S/E/W labels —
// not at the canvas half-width. The fetched map covers the full canvas, so
// its bbox must be scaled up proportionally, otherwise the configured range
// would land short of the actual outer ring.
constexpr int RADAR_OUTER_RADIUS_PX = MAP_SIZE_PX / 2 - 30; // mirrors radar_outer_radius()

constexpr const char *CACHE_PATH = "/radarmap.jpg";
constexpr const char *ARCGIS_HOST = "services.arcgisonline.com";

enum class Style : uint8_t {
    DARK_GRAY = 0,
    LIGHT_GRAY = 1,
    STREETS = 2,
    IMAGERY = 3,
    STREETS_HIGHLIGHT = 4, // World_Street_Map, recolored on-device: black bg, cyan roads, blue water, white labels
};

// Esri static-export MapServer path for each style, all under the same
// no-API-key services.arcgisonline.com host. STREETS_HIGHLIGHT fetches the
// same source tiles as STREETS — only the on-device color remap differs.
inline const char *style_path(Style s) {
    switch (s) {
        case Style::LIGHT_GRAY:        return "Canvas/World_Light_Gray_Base";
        case Style::STREETS:           return "World_Street_Map";
        case Style::STREETS_HIGHLIGHT: return "World_Street_Map";
        case Style::IMAGERY:           return "World_Imagery";
        case Style::DARK_GRAY:
        default:                       return "Canvas/World_Dark_Gray_Base";
    }
}

inline const char *style_name(Style s) {
    switch (s) {
        case Style::LIGHT_GRAY:        return "lightgray";
        case Style::STREETS:           return "streets";
        case Style::STREETS_HIGHLIGHT: return "streetshighlight";
        case Style::IMAGERY:           return "imagery";
        case Style::DARK_GRAY:
        default:                       return "darkgray";
    }
}

inline Style style_from_name(const String &name) {
    if (name == "lightgray") return Style::LIGHT_GRAY;
    if (name == "streets") return Style::STREETS;
    if (name == "streetshighlight") return Style::STREETS_HIGHLIGHT;
    if (name == "imagery") return Style::IMAGERY;
    return Style::DARK_GRAY;
}

// Runtime-configurable settings (loaded from config.ini, editable via the
// device webserver — see main.cpp parseConfigValue/saveConfigValuesSPIFFS).
inline bool s_enabled = true;
inline Style s_style = Style::DARK_GRAY;
inline float s_range_nmi = 40.0f; // ground distance (radius) shown at the radar's outer ring

inline lv_color_t *s_map_buf = nullptr; // RGB565, MAP_SIZE_PX x MAP_SIZE_PX, PSRAM
inline bool s_map_ready = false;

// ── TJpgDec plumbing: decode straight into s_map_buf, converting RGB888->RGB565 ──

struct DecodeSource {
    File file;
};

inline size_t jpg_input(JDEC *jd, uint8_t *buff, size_t ndata) {
    DecodeSource *src = (DecodeSource *)jd->device;
    if (!buff) {
        src->file.seek(src->file.position() + ndata);
        return ndata;
    }
    return src->file.read(buff, ndata);
}

// STREETS_HIGHLIGHT remap: recolors Esri's World_Street_Map tiles into a
// black-background radar-style backdrop. Thresholds were picked by sampling
// real tile pixels (roads are a warm salmon/red-brown fill+outline, water is
// a flat light blue, label text is near-black low-saturation, everything
// else — land, vegetation, buildings — is tan/green and gets dropped to
// black) — not derived from any documented Esri color spec, so a future
// basemap update could shift them; the four output colors below stay
// visually distinct from each other if that happens.
constexpr uint32_t ROAD_HIGHLIGHT_COLOR = 0x00E5FF;  // cyan
constexpr uint32_t WATER_HIGHLIGHT_COLOR = 0x1E5090; // dark blue
constexpr uint32_t LABEL_HIGHLIGHT_COLOR = 0xFFFFFF; // white

inline lv_color_t remap_streets_highlight(uint8_t r, uint8_t g, uint8_t b) {
    bool is_water = (int)b > (int)r + 15;
    bool is_road = !is_water && (int)r > (int)b + 55 && r >= g;
    bool is_label = !is_water && !is_road && r < 60 && g < 60 && b < 60;

    if (is_road) return lv_color_hex(ROAD_HIGHLIGHT_COLOR);
    if (is_water) return lv_color_hex(WATER_HIGHLIGHT_COLOR);
    if (is_label) return lv_color_hex(LABEL_HIGHLIGHT_COLOR);
    return lv_color_hex(0x000000);
}

inline int jpg_output(JDEC *jd, void *bitmap, JRECT *rect) {
    (void)jd;
    const uint8_t *src = (const uint8_t *)bitmap; // RGB888 rows (JD_FORMAT 0)
    const int row_width = rect->right - rect->left + 1;
    const bool highlight = (s_style == Style::STREETS_HIGHLIGHT);

    for (int y = rect->top; y <= rect->bottom; y++) {
        if (y >= MAP_SIZE_PX) continue;
        lv_color_t *dst_row = s_map_buf + y * MAP_SIZE_PX + rect->left;
        for (int x = 0; x < row_width; x++) {
            if (rect->left + x >= MAP_SIZE_PX) break;
            uint8_t r = src[0], g = src[1], b = src[2];
            src += 3;
            dst_row[x] = highlight ? remap_streets_highlight(r, g, b) : lv_color_make(r, g, b);
        }
    }
    return 1;
}

// Decodes /radarmap.jpg from SPIFFS into s_map_buf. Returns false on any error.
inline bool decode_cached_jpeg() {
    if (!s_map_buf) return false;

    File f = SPIFFS.open(CACHE_PATH, FILE_READ);
    if (!f) {
        DEBUG_PRINTLN("radarmap: no cached file");
        return false;
    }

    static JDEC jd;
    static uint8_t work[4096];
    DecodeSource src{f};

    JRESULT res = jd_prepare(&jd, jpg_input, work, sizeof(work), &src);
    if (res != JDR_OK) {
        DEBUG_PRINTLN("radarmap: jd_prepare failed: " + String((int)res));
        f.close();
        return false;
    }

    res = jd_decomp(&jd, jpg_output, 0);
    f.close();

    if (res != JDR_OK) {
        DEBUG_PRINTLN("radarmap: jd_decomp failed: " + String((int)res));
        return false;
    }

    DEBUG_PRINTLN("radarmap: decoded from cache");
    return true;
}

// Computes the Web Mercator bbox for myLat/myLon at s_range_nmi and fetches
// a fresh JPEG from Esri's static export service (using the selected
// s_style layer), saving it to SPIFFS.
inline bool fetch_and_cache() {
    constexpr double EARTH_RADIUS_M = 6378137.0;
    double bbox_half_width_nmi =
        (double)s_range_nmi * (double)(MAP_SIZE_PX / 2) / (double)RADAR_OUTER_RADIUS_PX;
    double range_m = bbox_half_width_nmi * 1852.0;

    double x = radians(myLon) * EARTH_RADIUS_M;
    double y = log(tan(PI / 4.0 + radians(myLat) / 2.0)) * EARTH_RADIUS_M;

    char pathBuf[400];
    snprintf(pathBuf, sizeof(pathBuf),
             "/arcgis/rest/services/%s/MapServer/export"
             "?bbox=%.1f,%.1f,%.1f,%.1f&bboxSR=102100&imageSR=102100"
             "&size=%d,%d&format=jpg&transparent=false&f=image",
             style_path(s_style),
             x - range_m, y - range_m, x + range_m, y + range_m,
             MAP_SIZE_PX, MAP_SIZE_PX);

    WiFiClientSecure client;
    client.setInsecure(); // no cert pinning available on-device; matches project's existing WiFiClientSecure usage
    client.setTimeout(15000);

    DEBUG_PRINTLN("radarmap: connecting to " + String(ARCGIS_HOST));
    if (!client.connect(ARCGIS_HOST, 443)) {
        DEBUG_PRINTLN("radarmap: connect failed");
        return false;
    }

    client.print(String("GET ") + pathBuf + " HTTP/1.1\r\n" +
                 "Host: " + ARCGIS_HOST + "\r\n" +
                 "Connection: close\r\n\r\n");

    unsigned long start = millis();
    while (client.connected() && !client.available()) {
        if (millis() - start > 15000) {
            DEBUG_PRINTLN("radarmap: response timeout");
            client.stop();
            return false;
        }
        delay(10);
    }

    // Skip HTTP headers.
    String statusLine = client.readStringUntil('\n');
    if (statusLine.indexOf("200") < 0) {
        DEBUG_PRINTLN("radarmap: HTTP error: " + statusLine);
        client.stop();
        return false;
    }
    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") break;
    }

    File f = SPIFFS.open(CACHE_PATH, FILE_WRITE);
    if (!f) {
        DEBUG_PRINTLN("radarmap: failed to open cache file for write");
        client.stop();
        return false;
    }

    uint8_t buf[512];
    size_t total = 0;
    while (client.connected() || client.available()) {
        size_t n = client.available();
        if (n > 0) {
            size_t toRead = n > sizeof(buf) ? sizeof(buf) : n;
            size_t got = client.read(buf, toRead);
            f.write(buf, got);
            total += got;
        } else if (!client.connected()) {
            break;
        } else {
            delay(1);
        }
    }
    f.close();
    client.stop();

    DEBUG_PRINTLN("radarmap: fetched " + String(total) + " bytes");
    return total > 0;
}

// Call once at boot, after WiFi + SPIFFS are up. Loads from SPIFFS cache if
// present; otherwise fetches from ArcGIS and caches for next boot. No-op if
// the basemap is disabled via config.
inline void init() {
    s_map_ready = false;
    if (!s_enabled) {
        DEBUG_PRINTLN("radarmap: disabled via config");
        return;
    }

    if (!s_map_buf) {
        s_map_buf = (lv_color_t *)heap_caps_malloc(
            (size_t)MAP_SIZE_PX * MAP_SIZE_PX * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    }
    if (!s_map_buf) {
        DEBUG_PRINTLN("radarmap: PSRAM alloc failed");
        return;
    }

    if (decode_cached_jpeg()) {
        s_map_ready = true;
        return;
    }

    if (WiFi.status() == WL_CONNECTED && fetch_and_cache() && decode_cached_jpeg()) {
        s_map_ready = true;
    } else {
        DEBUG_PRINTLN("radarmap: unavailable, radar will render without basemap");
    }
}

// Deletes the SPIFFS cache and immediately re-fetches from ArcGIS using the
// current style/range/enabled settings. Used by the webserver's "refresh
// map" action (e.g. after the device moved, or a style/range change).
inline bool refresh() {
    if (SPIFFS.exists(CACHE_PATH)) {
        SPIFFS.remove(CACHE_PATH);
    }
    init();
    return s_map_ready;
}

inline bool ready() { return s_map_ready; }
inline lv_color_t *buffer() { return s_map_buf; }

} // namespace radarmap
