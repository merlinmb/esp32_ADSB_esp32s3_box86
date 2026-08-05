# esp32_ADSB_esp32s3_box86

This project implements an ADS-B (Automatic Dependent Surveillance-Broadcast) information display on an ESP32-S3 "box86" 480×480 round-corner touchscreen board.
It polls a local ADS-B receiver (e.g. dump1090/readsb/tar1090's `aircraft.json`), tracks nearby air traffic, and renders it as a carousel of live status screens plus a rotating radar sweep with an optional real-world basemap — built for aviation enthusiasts and hobbyists running their own receiver.

## Features

- **Real-Time Flight Data**: Polls a local `aircraft.json` source over plain HTTP every 30 seconds (5 seconds while the radar screen is active), streaming-parses only the fields it needs directly off the TCP socket to keep heap usage low.
- **Screen Carousel**: Cycles automatically through System Info, Closest Aircraft, Live Airspace Statistics, and Radar screens (each individually enable/disable-able in code), plus dedicated priority cards for up to 4 aircraft squawking an emergency code.
- **Radar Screen**: A rotating sweep display (FlightRadar-style) showing tracked aircraft as rotated plane/helicopter icons at true bearing/range, with a configurable range ring, optional animated sweep line with fading trail, and tap-to-inspect detail popups.
- **Live Radar Basemap**: Optionally fetches and caches a real terrain/street basemap (Esri ArcGIS or Geoapify) centered on the receiver's location, aligned to the radar's range rings, with a choice of map styles.
- **Wi-Fi Connectivity**: Connects to a primary/fallback access point pair, with automatic reconnection checks.
- **MQTT Integration**: Publishes status and subscribes to command topics (reset, brightness, info) for remote monitoring and control.
- **Touch Controls**: Tap zones on the touchscreen for toggling backlight, cycling screens, jumping to system info, forcing a data refresh, and rebooting — with debounced single/double-tap and long-press detection.
- **Web Server**: Hosts a local configuration/monitoring web UI plus a browser-based firmware upload page.
- **OTA Updates**: Supports both browser-upload and Arduino IDE/PlatformIO over-the-air firmware updates.
- **Time Synchronization**: Synchronizes with an NTP server and auto-adjusts for UK British Summer Time (BST).
- **Persistent Configuration**: Settings (data source URL, screen flip, brightness, map style/range/enabled, radar scan line, radar dwell time) are saved to a SPIFFS `config.ini` and reloaded on boot.

## Hardware

| Component | Detail |
|-----------|--------|
| MCU | ESP32-S3 DevKitC-1 N16R16 (16 MB flash, 8 MB PSRAM) |
| Display | 480×480 RGB panel, ST7701 driver |
| Touch | TAMC GT911 capacitive controller (I2C) |
| Interface | SPI (panel init) + parallel RGB (pixel data) |

### Pin assignments

**Display (RGB parallel)**

| Signal | GPIO |
|--------|------|
| DE | 18 |
| VSYNC | 17 |
| HSYNC | 16 |
| PCLK | 21 |
| R0–R4 | 11, 12, 13, 14, 0 |
| G0–G5 | 8, 20, 3, 46, 9, 10 |
| B0–B4 | 4, 5, 6, 7, 15 |
| Backlight | 38 |

**Display (SPI init)**

| Signal | GPIO |
|--------|------|
| CS | 39 |
| SCK | 48 |
| MOSI | 47 |

**Touch (I2C)**

| Signal | GPIO |
|--------|------|
| SDA | 19 |
| SCL | 45 |

## Software Requirements

- **PlatformIO**: Used for building and uploading the firmware.
- **Arduino Framework**: The project is built using the Arduino framework for ESP32 (via the [pioarduino](https://github.com/pioarduino/platform-espressif32) Espressif32 platform fork).
- **Arduino_GFX**: Drives the ST7701 RGB panel via a custom init sequence for the ESP32-4848S040 panel.
- **LVGL v8.3.11**: UI framework used for all on-screen rendering (double-buffered draw buffer in PSRAM, custom flush callback writing straight into the RGB framebuffer).
- **TAMC GT911**: Touch controller driver, with automatic I2C bus recovery if the touch controller wedges (it has no wired reset/interrupt line).

### Dependencies

| Library | Purpose |
|---------|---------|
| [Arduino_GFX](https://github.com/moononournation/Arduino_GFX) | ST7701 RGB panel driver |
| [LVGL v8.3.11](https://github.com/lvgl/lvgl) | UI framework (includes TJpgDec, used to decode the radar basemap) |
| [TAMC GT911](https://github.com/TAMCTec/gt911-arduino) | Touch controller |
| [ArduinoJson](https://arduinojson.org/) (v7) | Streaming ADS-B JSON parsing, PSRAM-backed allocator |
| [NTPClient](https://github.com/taranais/NTPClient) | Time sync |
| [Time](https://github.com/PaulStoffregen/Time) | `hour()` / `minute()` helpers |
| [Timezone](https://github.com/JChristensen/Timezone) | BST/UK daylight-saving adjustment |
| [PubSubClient](https://github.com/knolleary/pubsubclient) | MQTT integration |
| [Regexp](https://github.com/nickgammon/Regexp) | Pattern matching helpers |
| [ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S) | Vendored dependency (not currently used by app logic) |

### Radar basemap providers

The radar screen's optional background map is fetched over HTTPS (no local API key needed for the Esri styles) from one of:

- **Esri ArcGIS** static map export — Dark Gray, Light Gray, Streets, and World Imagery styles.
- **Geoapify** static map API (dark-matter style, `"Streets (black bg, highlighted roads)"` option) — requires a free Geoapify API key set as `GEOAPIFY_KEY` in `connectionDetails.h`.

The fetched JPEG is decoded once and cached to SPIFFS (`/radarmap-v5.jpg`) so later boots don't need network access unless the style/range changes or a manual refresh is requested.

## Installation

1. Clone this repository to your local machine.
2. Install [PlatformIO](https://platformio.org/) in your IDE (e.g., Visual Studio Code).
3. Open the project folder in your IDE.
4. Configure your own settings as per the Configuration section below.
5. Build and upload the firmware: `pio run --target upload` (default upload port is `COM8` — change `upload_port` in `platformio.ini` to match your system).

## Configuration

Before building the project, you need to configure your connection details:

1. Copy the `connectionDetails.example.h` file located in the `include/` directory and rename it to `connectionDetails.h` (this file is gitignored, so your credentials stay local).
2. Open the newly created `connectionDetails.h` file and update the following fields with your own information:
   - **Wi-Fi Credentials**: Set `WIFI_ACCESSPOINT`/`WIFI_ACCESSPOINT_PASSWORD` (and the fallback `WIFI_ACCESSPOINT1`/`WIFI_ACCESSPOINT_PASSWORD1`) to your Wi-Fi network's SSID and password.
   - **MQTT Settings**: Configure `MQTT_SERVERADDRESS` and `MQTT_CLIENTNAME` for your MQTT broker.
   - **OTA Update Credentials**: Set `ARDUINO_OTA_UPDATE_USERNAME` and `ARDUINO_OTA_UPDATE_PASSWORD` for over-the-air updates.
   - **Geoapify API key** (optional): Add a `GEOAPIFY_KEY` constant if you want to use the "Streets (highlighted roads)" radar basemap style; the Esri-backed styles need no key.
3. In `include/merlinFlightStats.h`, set `host`/`path`/`port` to your ADS-B data source (default expects a `dump1090`/`readsb`-style `aircraft.json` endpoint) and `myLat`/`myLon` to your receiver's coordinates — these drive distance, bearing, and radar-basemap centering.
4. `data/config.ini` seeds the SPIFFS filesystem with the initial `jsonURI` and `radardwellseconds`; upload it with PlatformIO's "Upload Filesystem Image" task (or let the device fall back to its defaults and configure everything via the web UI instead).

This step is essential to ensure the project works with your specific network and environment.

## Usage

1. Power on the board.
2. The device initializes and streams startup/status messages onto the screen (Wi-Fi connect, web server, OTA, MQTT, filesystem, radar basemap load, time sync) before the main UI takes over.
3. Connect to the local web server (`http://<device-ip>/` or `http://espADSBMonitor.local/` via mDNS) for configuration or monitoring.
4. View real-time flight data on the display, which auto-cycles between screens.

### Web configuration UI

The web server exposes a dark-themed settings page with:

- **Data Source** — the ADS-B `aircraft.json` URL/host.
- **Display** — backlight toggle, radar scan-line toggle, radar dwell time (seconds).
- **Radar Map** — background map enable toggle, map style selector, map range (nmi), and a "Refresh map" action to re-fetch the basemap immediately.
- **Connection** — live Wi-Fi SSID/signal strength, IP/MAC address, firmware version, and last MQTT message received/published.
- A separate `/serverIndex` page for uploading new firmware binaries directly from the browser.

Saved settings persist to SPIFFS (`config.ini`) and are reloaded on every boot. Changing a map style/range/enabled setting triggers an immediate (blocking, up to ~15s) basemap re-fetch.

### Touch controls

There are no physical buttons — all interaction is via two small tap zones in the bottom corners of the touchscreen (invisible, 32×32px):

| Gesture | Zone | Action |
|---------|------|--------|
| Tap | Bottom-left corner | Toggle the backlight on or off |
| Double-tap | Bottom-left corner | Jump to (or back from) the system info screen |
| Long-press | Bottom-left corner | Reboot the device |
| Tap | Bottom-right corner | Advance to the next screen |
| Double-tap | Bottom-right corner | Force an immediate ADS-B data refresh |
| Tap (radar screen only) | Anywhere near a tracked aircraft icon | Show a 5-second detail popup (identifier, description, altitude/speed/distance/squawk, status, route) for the nearest aircraft |

## Display & UI Details

The UI is built entirely in LVGL on a single 480×480 canvas, styled as a dark cockpit/HUD theme (near-black background, cyan/blue-gray accents, altitude-graded aircraft colors). Key intricacies:

- **Screen carousel**: `main.cpp` advances `_currentFrame` through System Info → Closest Aircraft → Live Airspace Stats → Radar (Map is defined but disabled by default) every 10 seconds, or immediately after a data update or touch action. Emergency aircraft (squawk 7500/7600/7700/2000/0030) get dedicated priority cards inserted into the rotation, red-bordered and shown ahead of the next normal screen.
- **Radar screen**: aircraft within the configured range are plotted by true bearing/distance from the receiver as small rotated icons (fixed-wing "swept wing" glyph or an "X-rotor" helicopter glyph, auto-detected from ICAO category `A7` or the type description). Icon rotation reflects true track/heading when available. An optional green sweep line with an 18-segment fading trail rotates once every 4 seconds (toggleable; paused/resumed rather than destroyed when leaving/returning to the screen to avoid flicker). Range rings are drawn at 1/3, 2/3, and full configured range: N/S/E/W labels and the range annotation are overlaid outside the LVGL canvas as plain labels. Tapping near a plotted aircraft opens an info card that auto-dismisses after 5 seconds.
- **Radar basemap**: when enabled, a real map image (Esri or Geoapify) is decoded straight into a persistent RGB565 PSRAM buffer and blitted under the radar overlay every render, so the rings/aircraft/sweep always sit on real terrain aligned to true north and the configured range. The Dark Gray Esri style is additionally recolored client-side (roads → cyan, water → dark blue, labels → white) for contrast against the black HUD background.
- **Aircraft cards** (Closest Aircraft / Emergency): show identifier, aircraft type/description, a "TRACKING" badge with the total aircraft count, distance/altitude/squawk metrics, a color-coded ascending/descending/cruising status chip, and — except on emergency cards — a small compass "look direction" widget (rendered on its own tiny LVGL canvas) pointing toward the aircraft with elevation-above-horizon in degrees.
- **Live Airspace Statistics**: a fixed table of fastest/slowest/highest/lowest/closest/farthest tracked aircraft plus a live emergency count (green "NONE" or red count).
- **Altitude color coding**: a continuous gradient from orange (ground level) through yellow/green/cyan/blue to violet (35,000+ ft), used consistently for aircraft icons and altitude metrics across every screen.
- **Empty state**: when no aircraft are currently tracked, a "NO AIRCRAFT" panel is shown instead of the selected screen's content, bounced vertically over time to avoid OLED/LCD burn-in.
- **Startup log**: before the main UI initializes, boot/status messages scroll in a 14-line monospace log directly on the panel, useful for diagnosing Wi-Fi/SPIFFS/basemap issues without a serial console.
- **Clock**: a persistent bottom-center HH:MM:SS clock overlays every screen except the radar screen (hidden there to avoid obscuring the sweep), synced via NTP with automatic BST adjustment.
- **Rendering pipeline**: LVGL's flush callback writes directly into the RGB panel's framebuffer (with cache write-back) rather than going through a generic display driver, and the aircraft-data fetch runs in a separate FreeRTOS task (pinned to core 1, below `loopTask` priority) so a slow/blocking network fetch never stalls touch input or frame rendering; a mutex guards the shared flight-stats struct during the handoff.

## Development

### File Structure

- `src/main.cpp`: Application entry point — setup/loop, config load/save, the ADS-B fetch task, MQTT callbacks, and the web server route handlers.
- `src/display.cpp`: The entire LVGL UI layer — display/touch driver init, all screen renderers, the radar sweep/basemap compositing, and touch-zone event handling.
- `src/generated_fonts/`: Pre-generated LVGL C font files (Inter, JetBrains Mono, and a symbol font) used across the UI.
- `include/display.h`: Display-layer public API, screen-carousel frame enum, and per-screen enable flags.
- `include/merlinFlightStats.h`: Aircraft/flight-stats data structures, streaming ADS-B JSON fetch + parse, haversine distance, emergency-squawk detection.
- `include/merlinRadarMap.h`: Radar basemap fetch (Esri/Geoapify), SPIFFS JPEG caching, and JPEG-to-RGB565 decoding/recoloring.
- `include/merlinNetwork.h`: Wi-Fi connection management (primary/fallback AP) and MQTT setup/publish helpers.
- `include/merlinUpdateWebServer.h`: Web server/OTA-update scaffolding, embedded HTML/CSS for the config UI and firmware-upload page.
- `include/ui_fonts.h`: Extern declarations for the generated LVGL fonts.
- `include/lv_conf.h`: LVGL build configuration.
- `include/connectionDetails.example.h`: Template for the gitignored `connectionDetails.h` (Wi-Fi, MQTT, OTA, and Geoapify credentials).
- `data/`: Files uploaded to the SPIFFS filesystem (`config.ini` seed values).
- `docs/`: Design/reference notes.

### Key Functions

- `setup()` (`main.cpp`): Initializes the display, Wi-Fi, web server, OTA, mDNS, MQTT, SPIFFS config, radar basemap, NTP time, and spawns the fetch task.
- `loop()` (`main.cpp`): Drives LVGL ticks, triggers periodic/forced ADS-B fetches (faster cadence on the radar screen), advances the screen carousel, updates the clock, and services MQTT/OTA/web-server clients.
- `fetchTask()` (`main.cpp`): FreeRTOS task on core 1 that blocks on a notification, fetches + parses flight data, and swaps it into the shared `_flightStats` struct under mutex.
- `parseConfigValue()` / `saveConfigValuesSPIFFS()` / `loadCustomParamsSPIFFS()` (`main.cpp`): Read/write the persisted SPIFFS `config.ini`, shared by both boot-time loading and the web `/set` endpoint.
- `display_init()` (`display.cpp`): Configures the Arduino_GFX/LVGL/touch display stack, including the ST7701 panel init sequence and PSRAM draw buffer.
- `display_render_frame()` (`display.cpp`): Dispatches to the renderer for the currently selected screen (`render_sysinfo`, `render_aircraft_card`, `render_topstats`, `render_map`, `render_radar`).
- `radarmap::init()` / `radarmap::refresh()` (`merlinRadarMap.h`): Load the cached basemap at boot, or fetch a fresh one and re-cache it.
- `fetchFlightData()` / `processFlightData()` (`merlinFlightStats.h`): Stream-parse the ADS-B JSON source and compute derived stats (fastest/slowest/highest/lowest/closest/farthest/emergencies).
- `setupWebServer()` (`main.cpp`): Registers the `/`, `/set`, `/refreshmap`, `/reset`, `/resetSettings`, `/update`, and `/serverIndex` routes.
- `setupOTA()` (`merlinUpdateWebServer.h`): Configures Arduino OTA update handling.

## Contributing

Contributions are welcome! Feel free to open issues or submit pull requests to improve the project.

## License

This project is licensed under the MIT License. See the `LICENSE` file for details.

## Acknowledgments

- [PlatformIO](https://platformio.org/) for simplifying embedded development.
- The open-source community for providing tools and libraries that make this project possible.
