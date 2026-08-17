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

extern const char *GEOAPIFY_KEY;

namespace radarmap {

constexpr int MAP_SIZE_PX = 480; // matches DISPLAY_WIDTH/HEIGHT
constexpr int GEOAPIFY_VERTICAL_CROP_PX = 40;
constexpr int GEOAPIFY_MAP_HEIGHT_PX = MAP_SIZE_PX + GEOAPIFY_VERTICAL_CROP_PX * 2;

// The radar's outer ring is drawn at radar_outer_radius() (display.cpp),
// which is inset from the canvas edge to leave room for N/S/E/W labels —
// not at the canvas half-width. The fetched map covers the full canvas, so
// its bbox must be scaled up proportionally, otherwise the configured range
// would land short of the actual outer ring.
constexpr int RADAR_OUTER_RADIUS_PX = MAP_SIZE_PX / 2 - 30; // mirrors radar_outer_radius()

// Bump this name when the map rendering changes so an old cache cannot hide
// an updated style after a firmware upload.
constexpr const char *CACHE_PATH = "/radarmap-v5.jpg";
constexpr const char *CACHE_TEMP_PATH = "/radarmap-v5.tmp";
constexpr const char *ARCGIS_HOST = "services.arcgisonline.com";
constexpr const char *ARCGIS_FALLBACK_HOST = "server.arcgisonline.com";
constexpr const char *GEOAPIFY_HOST = "maps.geoapify.com";

enum class Style : uint8_t {
    DARK_GRAY = 0,
    LIGHT_GRAY = 1,
    STREETS = 2,
    IMAGERY = 3,
    STREETS_HIGHLIGHT = 4, // Geoapify dark-matter basemap
};

// Esri static-export MapServer path for each Esri-backed style.
inline const char *style_path(Style s) {
    switch (s) {
        case Style::LIGHT_GRAY:        return "Canvas/World_Light_Gray_Base";
        case Style::STREETS:           return "World_Street_Map";
        case Style::IMAGERY:           return "World_Imagery";
        case Style::DARK_GRAY:
        case Style::STREETS_HIGHLIGHT: return "Canvas/World_Dark_Gray_Base";
        default:                       return "World_Street_Map";
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
inline bool s_scan_line_enabled = false;
inline bool s_always_show_nearest_info = false;
inline bool s_simulate_emergency = false; // debug/demo: force the closest aircraft to appear as an emergency

inline lv_color_t *s_map_buf = nullptr; // RGB565, MAP_SIZE_PX x MAP_SIZE_PX, PSRAM
inline uint8_t *s_jpeg_work_buf = nullptr;
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

// Recolor the bright, neutral road linework in Esri's dark-gray basemap.
constexpr uint32_t ROAD_HIGHLIGHT_COLOR = 0x00E5FF;  // cyan
constexpr uint32_t WATER_HIGHLIGHT_COLOR = 0x1E5090; // dark blue
constexpr uint32_t LABEL_HIGHLIGHT_COLOR = 0xFFFFFF; // white
constexpr uint32_t ROAD_DARK_COLOR = 0x38464C;
constexpr uint32_t WATER_DARK_COLOR = 0x102A38;
constexpr uint32_t LABEL_DARK_COLOR = 0x89949A;

inline lv_color_t remap_streets(uint8_t r, uint8_t g, uint8_t b, bool highlight) {
    int max_channel = max(r, max(g, b));
    int min_channel = min(r, min(g, b));
    int chroma = max_channel - min_channel;
    bool is_water = (int)b > (int)r + 15;
    bool is_road = !is_water && max_channel >= 45 && chroma <= 12;
    bool is_label = !is_water && !is_road && r < 60 && g < 60 && b < 60;

    if (highlight) {
        if (is_road) return lv_color_hex(ROAD_HIGHLIGHT_COLOR);
        if (is_water) return lv_color_hex(WATER_HIGHLIGHT_COLOR);
        if (is_label) return lv_color_hex(LABEL_HIGHLIGHT_COLOR);
    } else {
        if (is_road) return lv_color_hex(ROAD_DARK_COLOR);
        if (is_water) return lv_color_hex(WATER_DARK_COLOR);
        if (is_label) return lv_color_hex(LABEL_DARK_COLOR);
    }
    return lv_color_hex(0x000000);
}

inline int jpg_output(JDEC *jd, void *bitmap, JRECT *rect) {
    (void)jd;
    const uint8_t *src = (const uint8_t *)bitmap; // RGB888 rows (JD_FORMAT 0)
    const int row_width = rect->right - rect->left + 1;
    const bool remap = s_style == Style::DARK_GRAY;
    const int crop_top = s_style == Style::STREETS_HIGHLIGHT ? GEOAPIFY_VERTICAL_CROP_PX : 0;

    for (int y = rect->top; y <= rect->bottom; y++) {
        const int dst_y = y - crop_top;
        for (int x = 0; x < row_width; x++) {
            uint8_t r = src[0], g = src[1], b = src[2];
            src += 3;
            const int dst_x = rect->left + x;
            if (dst_x < MAP_SIZE_PX && dst_y >= 0 && dst_y < MAP_SIZE_PX) {
                s_map_buf[dst_y * MAP_SIZE_PX + dst_x] =
                    remap ? remap_streets(r, g, b, false) : lv_color_make(r, g, b);
            }
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

    if (!s_jpeg_work_buf) {
        s_jpeg_work_buf = (uint8_t *)heap_caps_malloc(4096, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!s_jpeg_work_buf) {
        DEBUG_PRINTLN("radarmap: JPEG work buffer alloc failed");
        f.close();
        return false;
    }

    static JDEC jd;
    DecodeSource src{f};

    JRESULT res = jd_prepare(&jd, jpg_input, s_jpeg_work_buf, 4096, &src);
    if (res != JDR_OK) {
        DEBUG_PRINTLN("radarmap: jd_prepare failed: " + String((int)res));
        f.close();
        SPIFFS.remove(CACHE_PATH);
        return false;
    }

    res = jd_decomp(&jd, jpg_output, 0);
    f.close();

    if (res != JDR_OK) {
        DEBUG_PRINTLN("radarmap: jd_decomp failed: " + String((int)res));
        SPIFFS.remove(CACHE_PATH);
        return false;
    }

    DEBUG_PRINTLN("radarmap: decoded from cache");
    return true;
}

// Fetches a fresh JPEG from the selected map provider, saving it to SPIFFS.
inline bool fetch_and_cache() {
    constexpr double EARTH_RADIUS_M = 6378137.0;
    double bbox_half_width_nmi =
        (double)s_range_nmi * (double)(MAP_SIZE_PX / 2) / (double)RADAR_OUTER_RADIUS_PX;
    double range_m = bbox_half_width_nmi * 1852.0;

    double x = radians(myLon) * EARTH_RADIUS_M;
    double y = log(tan(PI / 4.0 + radians(myLat) / 2.0)) * EARTH_RADIUS_M;

    char pathBuf[400];
    const char *const arcgis_hosts[] = {ARCGIS_HOST, ARCGIS_FALLBACK_HOST};
    const char *const geoapify_hosts[] = {GEOAPIFY_HOST};
    const char *const *hosts = arcgis_hosts;
    size_t host_count = sizeof(arcgis_hosts) / sizeof(arcgis_hosts[0]);

    if (s_style == Style::STREETS_HIGHLIGHT) {
        constexpr double WEB_MERCATOR_METERS_PER_PIXEL = 156543.03392;
        double zoom = log2(WEB_MERCATOR_METERS_PER_PIXEL * cos(radians(myLat)) * MAP_SIZE_PX /
                           (2.0 * range_m));
        zoom = constrain(zoom, 1.0, 20.0);
        snprintf(pathBuf, sizeof(pathBuf),
                 "/v1/staticmap?style=dark-matter&width=%d&height=%d&format=jpeg"
                 "&center=lonlat:%.6f,%.6f&zoom=%.2f&apiKey=%s",
                 MAP_SIZE_PX, GEOAPIFY_MAP_HEIGHT_PX, myLon, myLat, zoom, GEOAPIFY_KEY);
        hosts = geoapify_hosts;
        host_count = sizeof(geoapify_hosts) / sizeof(geoapify_hosts[0]);
    } else {
        snprintf(pathBuf, sizeof(pathBuf),
                 "/arcgis/rest/services/%s/MapServer/export"
                 "?bbox=%.1f,%.1f,%.1f,%.1f&bboxSR=102100&imageSR=102100"
                 "&size=%d,%d&format=jpg&transparent=false&f=image",
                 style_path(s_style),
                 x - range_m, y - range_m, x + range_m, y + range_m,
                 MAP_SIZE_PX, MAP_SIZE_PX);
    }

    for (size_t host_index = 0; host_index < host_count; host_index++) {
        const char *host = hosts[host_index];
        WiFiClientSecure client;
        client.setInsecure();
        client.setTimeout(15000);
        client.setHandshakeTimeout(15);

        DEBUG_PRINTLN("radarmap: connecting to " + String(host));
        if (!client.connect(host, 443)) {
            char error[80];
            client.lastError(error, sizeof(error));
            DEBUG_PRINTLN("radarmap: TLS failed: " + String(error) +
                          " (free internal heap: " + String(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)) + ")");
            client.stop();
            continue;
        }

        client.print(String("GET ") + pathBuf + " HTTP/1.1\r\n" +
                     "Host: " + host + "\r\n" +
                     "Connection: close\r\n\r\n");

        unsigned long start = millis();
        while (client.connected() && !client.available()) {
            if (millis() - start > 15000) {
                DEBUG_PRINTLN("radarmap: response timeout");
                client.stop();
                break;
            }
            delay(10);
        }
        if (!client.available()) continue;

        String statusLine = client.readStringUntil('\n');
        if (statusLine.indexOf("200") < 0) {
            DEBUG_PRINTLN("radarmap: HTTP error: " + statusLine);
            client.stop();
            continue;
        }
        while (client.connected()) {
            String line = client.readStringUntil('\n');
            if (line == "\r") break;
        }

        SPIFFS.remove(CACHE_TEMP_PATH);
        File f = SPIFFS.open(CACHE_TEMP_PATH, FILE_WRITE);
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

        if (total > 0) {
            bool cache_removed = !SPIFFS.exists(CACHE_PATH) || SPIFFS.remove(CACHE_PATH);
            if (cache_removed && SPIFFS.rename(CACHE_TEMP_PATH, CACHE_PATH)) {
                DEBUG_PRINTLN("radarmap: fetched " + String(total) + " bytes");
                return true;
            }
        }

        SPIFFS.remove(CACHE_TEMP_PATH);
        DEBUG_PRINTLN("radarmap: failed to update cache");
        return false;
    }

    DEBUG_PRINTLN("radarmap: connect failed");
    return false;
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
    SPIFFS.remove(CACHE_TEMP_PATH);
    s_map_ready = false;

    if (!s_enabled) {
        DEBUG_PRINTLN("radarmap: disabled via config");
        return false;
    }

    if (!s_map_buf) {
        s_map_buf = (lv_color_t *)heap_caps_malloc(
            (size_t)MAP_SIZE_PX * MAP_SIZE_PX * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    }
    if (!s_map_buf) {
        DEBUG_PRINTLN("radarmap: PSRAM alloc failed");
        return false;
    }

    if (WiFi.status() == WL_CONNECTED && fetch_and_cache() && decode_cached_jpeg()) {
        s_map_ready = true;
    } else {
        DEBUG_PRINTLN("radarmap: refresh failed, retained previous cache");
    }
    return s_map_ready;
}

inline bool ready() { return s_map_ready; }
inline lv_color_t *buffer() { return s_map_buf; }

} // namespace radarmap
