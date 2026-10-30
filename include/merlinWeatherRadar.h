#pragma once

// Weather (precipitation radar) overlay for the radar screen, from
// RainViewer's free public API — no API key required.
//
// RainViewer can render a single image centred on a lat/lon, so instead of
// stitching map tiles we request exactly the radar's viewport:
//   1. GET weather-maps.json for the newest radar frame path.
//   2. GET {path}/512/{z}/{lat}/{lon}/2/1_0.png — a 512px PNG centred on
//      myLat/myLon. It spans the same ground as one 256px tile at zoom z (the
//      512 size only doubles resolution). z is the highest zoom (public API
//      max: 7) whose image still covers the whole screen at the current range.
//   3. Decode with lodepng (vendored in LVGL, LV_USE_PNG in lv_conf.h) and
//      resample into a 480x480 RGB565 + alpha overlay at the screen's exact
//      scale, so render_radar() blends it straight between basemap and
//      aircraft. Light precipitation is whitened, and all alpha is scaled
//      by OPACITY so the basemap stays visible through the clouds.
//
// Both RainViewer hosts serve plain HTTP, which keeps TLS out of fetchTask.

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <math.h>
#include <lvgl.h>

// lodepng.h has no extern "C" guard and pulls in <vector>/<string> under C++.
#define LODEPNG_NO_COMPILE_CPP
extern "C" {
#include "src/extra/libs/png/lodepng.h"
}

#include "merlinFlightStats.h" // myLat, myLon
#include "merlinRadarMap.h"    // s_range_nmi, RADAR_OUTER_RADIUS_PX

namespace weatherradar {

constexpr int OVERLAY_SIZE_PX = radarmap::MAP_SIZE_PX; // matches DISPLAY_WIDTH/HEIGHT
constexpr int IMAGE_SIZE_PX = 512;  // RainViewer image size (256 or 512)
constexpr int MAX_ZOOM = 7;         // RainViewer public API limit (8+ returns a placeholder image)
constexpr uint8_t OPACITY = 200;    // multiplied with the PNG's per-pixel alpha
constexpr uint8_t LIGHT_PRECIP_MIN_ALPHA = 90; // floor for the faint light-precipitation ramp
constexpr size_t MAX_BODY_BYTES = 512 * 1024;
constexpr unsigned long REFRESH_INTERVAL_MS = 10UL * 60UL * 1000UL; // RainViewer publishes every 10 min
constexpr unsigned long RETRY_INTERVAL_MS = 60UL * 1000UL;
constexpr const char *API_HOST = "api.rainviewer.com";
constexpr const char *API_PATH = "/public/weather-maps.json";
constexpr const char *TILE_HOST = "tilecache.rainviewer.com";

inline bool s_enabled = true; // toggled by tapping the cloud on the radar screen
inline bool s_ready = false;
inline lv_color_t *s_overlay_color = nullptr; // RGB565, OVERLAY_SIZE_PX^2, PSRAM
inline uint8_t *s_overlay_alpha = nullptr;    // 0 = no precipitation
inline unsigned long s_last_attempt_ms = 0;
inline float s_attempt_range_nmi = 0.0f;
inline float s_built_range_nmi = 0.0f;

// True when an overlay exists and matches the radar's current range.
inline bool overlay_valid() { return s_ready && s_built_range_nmi == radarmap::s_range_nmi; }

// True when the overlay is enabled and missing, stale, or built for a
// different radar range.
inline bool needs_refresh() {
    if (!s_enabled) return false;
    if (s_last_attempt_ms == 0 || s_attempt_range_nmi != radarmap::s_range_nmi) return true;
    return millis() - s_last_attempt_ms >= (overlay_valid() ? REFRESH_INTERVAL_MS : RETRY_INTERVAL_MS);
}

// Plain-HTTP GET into a PSRAM buffer sized from Content-Length. Caller frees
// with heap_caps_free(). Returns nullptr on any error.
inline uint8_t *http_get(const char *host, const String &path, size_t *out_len) {
    WiFiClient client;
    client.setTimeout(10000);
    if (!client.connect(host, 80)) {
        DEBUG_PRINTLN("weather: connect failed: " + String(host));
        return nullptr;
    }
    client.print(String("GET ") + path + " HTTP/1.0\r\n" +
                 "Host: " + host + "\r\n" +
                 "Connection: close\r\n\r\n");

    String status = client.readStringUntil('\n');
    if (status.indexOf(" 200") < 0) {
        DEBUG_PRINTLN("weather: HTTP error: " + status);
        return nullptr;
    }

    size_t content_length = 0;
    for (;;) {
        String line = client.readStringUntil('\n');
        if (line == "\r") break;
        if (line.length() == 0) {
            DEBUG_PRINTLN("weather: timed out reading headers");
            return nullptr;
        }
        line.toLowerCase();
        if (line.startsWith("content-length:")) content_length = line.substring(15).toInt();
    }
    if (content_length == 0 || content_length > MAX_BODY_BYTES) {
        DEBUG_PRINTLN("weather: bad content length: " + String(content_length));
        return nullptr;
    }

    uint8_t *buf = (uint8_t *)heap_caps_malloc(content_length, MALLOC_CAP_SPIRAM);
    if (!buf) {
        DEBUG_PRINTLN("weather: body alloc failed");
        return nullptr;
    }
    if (client.readBytes(buf, content_length) != content_length) {
        DEBUG_PRINTLN("weather: short read");
        heap_caps_free(buf);
        return nullptr;
    }
    *out_len = content_length;
    return buf;
}

// Path of the newest past radar frame, e.g. "/v2/radar/fc3bfba51ee1".
inline String fetch_latest_frame_path() {
    size_t len = 0;
    uint8_t *body = http_get(API_HOST, API_PATH, &len);
    if (!body) return "";

    JsonDocument filter;
    filter["radar"]["past"][0]["path"] = true;
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, body, len, DeserializationOption::Filter(filter));
    heap_caps_free(body);
    if (error) {
        DEBUG_PRINTLN("weather: JSON parse failed: " + String(error.c_str()));
        return "";
    }

    JsonArray past = doc["radar"]["past"];
    if (past.size() == 0) return "";
    return String(past[past.size() - 1]["path"] | "");
}

// Fetches the newest radar frame for the current viewport and rebuilds the
// overlay. Network + decode run unlocked; only the final resample into the
// overlay buffers holds render_lock, since render_radar() reads them.
inline bool refresh(SemaphoreHandle_t render_lock) {
    s_last_attempt_ms = millis();
    if (s_last_attempt_ms == 0) s_last_attempt_ms = 1; // 0 means "never attempted"
    s_attempt_range_nmi = radarmap::s_range_nmi;

    if (!s_overlay_color) {
        s_overlay_color = (lv_color_t *)heap_caps_malloc(
            (size_t)OVERLAY_SIZE_PX * OVERLAY_SIZE_PX * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    }
    if (!s_overlay_alpha) {
        s_overlay_alpha = (uint8_t *)heap_caps_malloc(
            (size_t)OVERLAY_SIZE_PX * OVERLAY_SIZE_PX, MALLOC_CAP_SPIRAM);
    }
    if (!s_overlay_color || !s_overlay_alpha) {
        DEBUG_PRINTLN("weather: PSRAM alloc failed");
        return false;
    }

    String frame_path = fetch_latest_frame_path();
    if (frame_path.isEmpty()) return false;

    // Ground metres per screen pixel — radar_draw_aircraft() places
    // s_range_nmi at RADAR_OUTER_RADIUS_PX from the centre.
    const float range_nmi = radarmap::s_range_nmi;
    const double screen_mpp = range_nmi * 1852.0 / radarmap::RADAR_OUTER_RADIUS_PX;
    const double cos_lat = cos(radians(myLat));
    auto image_mpp = [&](int z) {
        return 156543.03392 * cos_lat / (double)(1 << z) / (IMAGE_SIZE_PX / 256);
    };
    int zoom = MAX_ZOOM;
    while (zoom > 0 && image_mpp(zoom) * IMAGE_SIZE_PX < screen_mpp * OVERLAY_SIZE_PX) zoom--;

    // Colour scheme 2 (Universal Blue) — the public API now serves only this
    // scheme whatever id is requested. Options 1_0 = smoothed, no snow colours.
    char path[96];
    snprintf(path, sizeof(path), "%s/%d/%d/%.4f/%.4f/2/1_0.png",
             frame_path.c_str(), IMAGE_SIZE_PX, zoom, myLat, myLon);
    DEBUG_PRINTLN("weather: fetching " + String(path));

    size_t png_len = 0;
    uint8_t *png = http_get(TILE_HOST, path, &png_len);
    if (!png) return false;

    unsigned char *rgba = nullptr; // allocated by lodepng via lv_mem_alloc
    unsigned w = 0, h = 0;
    unsigned err = lodepng_decode32(&rgba, &w, &h, png, png_len);
    heap_caps_free(png);
    if (err || w != IMAGE_SIZE_PX || h != IMAGE_SIZE_PX) {
        DEBUG_PRINTLN("weather: PNG decode failed: " + String(err));
        if (rgba) lv_mem_free(rgba);
        return false;
    }

    // Nearest-neighbour resample: both images share a centre, so mapping is a
    // pure scale (image px per screen px).
    const float scale = (float)(screen_mpp / image_mpp(zoom));
    const float img_half = IMAGE_SIZE_PX / 2.0f;
    const float ovl_half = OVERLAY_SIZE_PX / 2.0f;

    xSemaphoreTake(render_lock, portMAX_DELAY);
    for (int sy = 0; sy < OVERLAY_SIZE_PX; sy++) {
        const int iy = (int)floorf(img_half + (sy + 0.5f - ovl_half) * scale);
        for (int sx = 0; sx < OVERLAY_SIZE_PX; sx++) {
            const int ix = (int)floorf(img_half + (sx + 0.5f - ovl_half) * scale);
            const int i = sy * OVERLAY_SIZE_PX + sx;
            if (ix < 0 || iy < 0 || ix >= IMAGE_SIZE_PX || iy >= IMAGE_SIZE_PX) {
                s_overlay_alpha[i] = 0;
                continue;
            }
            // Light precipitation comes as a dim beige ramp with alpha rising
            // by intensity; actual rain is opaque, coloured by intensity.
            // Render the ramp as white with a lifted alpha so it reads as
            // cloud over a dark basemap; keep the rain colours.
            const uint8_t *p = rgba + ((size_t)iy * IMAGE_SIZE_PX + ix) * 4;
            uint8_t a = p[3];
            if (a == 255) {
                s_overlay_color[i] = lv_color_make(p[0], p[1], p[2]);
            } else {
                s_overlay_color[i] = lv_color_white();
                if (a) a = LIGHT_PRECIP_MIN_ALPHA + (a * (255 - LIGHT_PRECIP_MIN_ALPHA)) / 255;
            }
            s_overlay_alpha[i] = (uint8_t)((a * OPACITY) / 255);
        }
    }
    s_built_range_nmi = range_nmi;
    s_ready = true;
    xSemaphoreGive(render_lock);

    lv_mem_free(rgba);
    DEBUG_PRINTLN("weather: overlay updated (zoom " + String(zoom) + ")");
    return true;
}

// Blends the overlay into a full-screen RGB565 buffer. No-op when disabled
// or no overlay matching the current range has been fetched yet.
inline void blend_into(lv_color_t *dst) {
    if (!s_enabled || !overlay_valid()) return;
    for (int i = 0; i < OVERLAY_SIZE_PX * OVERLAY_SIZE_PX; i++) {
        const uint8_t a = s_overlay_alpha[i];
        if (a) dst[i] = lv_color_mix(s_overlay_color[i], dst[i], a);
    }
}

} // namespace weatherradar
