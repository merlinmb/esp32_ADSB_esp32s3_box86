# esp32_ADSB_esp32s3_box86

This project implements an ADS-B (Automatic Dependent Surveillance-Broadcast) information display on an ESP32-S3 "box86" 480×480 touchscreen board.
It receives and displays real-time flight data, providing a compact and visually appealing interface for aviation enthusiasts or hobbyists.

## Features

- **Real-Time Flight Data**: Displays ADS-B data received from nearby aircraft.
- **Wi-Fi Connectivity**: Connects to a network for additional functionality, such as MQTT communication and OTA updates.
- **MQTT Integration**: Publishes and subscribes to topics for remote monitoring and control.
- **Touch Controls**: Tap zones on the touchscreen for cycling screens, adjusting brightness, forcing a refresh, and rebooting.
- **Web Server**: Hosts a local web server for configuration and monitoring.
- **OTA Updates**: Supports over-the-air firmware updates for easy maintenance.
- **Time Synchronization**: Synchronizes with an NTP server to display accurate local time.

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
- **Arduino Framework**: The project is built using the Arduino framework for ESP32.
- **Arduino_GFX**: Drives the ST7701 RGB panel.
- **LVGL v8.3.11**: UI framework used for all on-screen rendering.
- **TAMC GT911**: Touch controller driver.

### Dependencies

| Library | Purpose |
|---------|---------|
| [Arduino_GFX](https://github.com/moononournation/Arduino_GFX) | ST7701 RGB panel driver |
| [LVGL v8.3.11](https://github.com/lvgl/lvgl) | UI framework |
| [TAMC GT911](https://github.com/TAMCTec/gt911-arduino) | Touch controller |
| [ArduinoJson](https://arduinojson.org/) | ADS-B JSON parsing |
| [NTPClient](https://github.com/taranais/NTPClient) | Time sync |
| [Time](https://github.com/PaulStoffregen/Time) | `hour()` / `minute()` helpers |
| [PubSubClient](https://github.com/knolleary/pubsubclient) | MQTT integration |

## Installation

1. Clone this repository to your local machine.
2. Install [PlatformIO](https://platformio.org/) in your IDE (e.g., Visual Studio Code).
3. Open the project folder in your IDE.
4. Configure your own settings as per the Configuration section below.
5. Build and upload the firmware: `pio run --target upload` (default upload port is `COM8` — change `upload_port` in `platformio.ini` to match your system).

## Configuration

Before building the project, you need to configure your connection details:

1. Copy the `connectionDetails.example.h` file located in the `include/` directory and rename it to `connectionDetails.h`.
2. Open the newly created `connectionDetails.h` file and update the following fields with your own information:
   - **Wi-Fi Credentials**: Set `WIFI_ACCESSPOINT`/`WIFI_ACCESSPOINT_PASSWORD` (and the fallback `WIFI_ACCESSPOINT1`/`WIFI_ACCESSPOINT_PASSWORD1`) to your Wi-Fi network's SSID and password.
   - **MQTT Settings**: Configure `MQTT_SERVERADDRESS` and `MQTT_CLIENTNAME` for your MQTT broker.
   - **OTA Update Credentials**: Set `ARDUINO_OTA_UPDATE_USERNAME` and `ARDUINO_OTA_UPDATE_PASSWORD` for over-the-air updates.

This step is essential to ensure the project works with your specific network and environment.

## Usage

1. Power on the board.
2. The device will initialize and display startup messages.
3. Connect to the local web server for additional configuration or monitoring.
4. View real-time flight data on the display.

### Touch controls

There are no physical buttons — all interaction is via tap zones on the touchscreen:

| Gesture | Zone | Action |
|---------|------|--------|
| Tap | Left half | Toggle the backlight on or off |
| Double-tap | Left half | Jump to the system info screen |
| Long-press | Left half | Reboot the device |
| Tap | Right half | Advance to the next screen |
| Double-tap | Right half | Force an immediate ADS-B data refresh |

## Development

### File Structure

- `src/`: Contains the main application code (`main.cpp` for app logic, `display.cpp` for the Arduino_GFX/LVGL/touch display layer).
- `include/`: Header files (`display.h`, `merlinFlightStats.h`, `merlinNetwork.h`, `merlinUpdateWebServer.h`, `lv_conf.h`).
- `data/`: Files for the SPIFFS filesystem.
- `docs/`: Design notes.

### Key Functions

- `setup()`: Initializes the system, including Wi-Fi, MQTT, and the display.
- `loop()`: Main application loop for handling updates, LVGL ticks, and events.
- `display_init()`: Configures the Arduino_GFX/LVGL/touch display stack.
- `display_render_frame()`: Renders the currently selected screen via LVGL.
- `setupWebServer()`: Sets up the local web server.
- `setupOTA()`: Configures over-the-air updates.

## Contributing

Contributions are welcome! Feel free to open issues or submit pull requests to improve the project.

## License

This project is licensed under the MIT License. See the `LICENSE` file for details.

## Acknowledgments

- [PlatformIO](https://platformio.org/) for simplifying embedded development.
- The open-source community for providing tools and libraries that make this project possible.
