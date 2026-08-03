// Call up the SPIFFS FLASH filing system
#include <FS.h>
#include <SPIFFS.h>

#define DEBUG 1

#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>

#include "time.h"
#include "connectionDetails.h"

#include <TimeLib.h>

// #define SECURE 1
#include "merlinNetwork.h"
#include "merlinUpdateWebServer.h"

#include "merlinFlightStats.h"
#include "merlinRadarMap.h"

#include "display.h"

#define WMAPNAME "ADSB_Monitor"

#define MAXBRIGHTNESS 255
#define MINBRIGHTNESS 0

int _brightnesses[5] = {0, 51, 115, 192, 255};
int _selectedBrightness = 4;

#define location "51.39502, -1.3387" // 97 Enborne Road

#define MCMDVERSION 1.4

bool _brightnessHigh;
byte _brightness = 100;

String _mqttPostFix = "";
float _batteryVoltage = 0;

boolean _forceUpdate = false;
boolean _forceRender = false;
boolean _mapSettingsChanged = false; // set by parseConfigValue when a mapstyle/maprange/mapenabled change needs a re-fetch

/* frames */
byte _currentFrame = FRAME_TOPSTATS;
#define MAXRENDER_EMERGENCIES 4

volatile bool _spritesNeedUpdate = false; // set by fetchTask after data swap; consumed by loop()

String _locationCode = "";

#define UPDATE_WIFICHECK_INTERVAL_MILLISECS 30000 // Update every 1 min
#define UPDATE_UI_FRAME_INTERVAL_MILLISECS 10000  // transition screen every ... milliseconds
#define UPDATE_ADSBS_INTERVAL_MILLISECS 30000     // Update every 30 seconds
#define UPDATE_TIME_INTERVAL_MILLISECS 900        // Update every <1sec

// Returns whether a given base carousel frame (non-emergency) is enabled per
// the flags in display.h.
bool isFrameEnabled(byte frame)
{
  switch (frame)
  {
  case FRAME_SYSINFO: return FRAME_SYSINFO_ENABLED;
  case FRAME_OVERVIEW: return FRAME_OVERVIEW_ENABLED;
  case FRAME_TOPSTATS: return FRAME_TOPSTATS_ENABLED;
  case FRAME_MAP: return FRAME_MAP_ENABLED;
  case FRAME_RADAR: return FRAME_RADAR_ENABLED;
  default: return true; // emergency slots aren't gated by these flags
  }
}

// Advances _currentFrame to the next enabled frame in the carousel, wrapping
// back to FRAME_OVERVIEW after the last emergency slot (or FRAME_RADAR if
// there are no emergencies). Skips disabled screens entirely.
void advanceToNextEnabledFrame(int emergencyCount)
{
  for (int attempts = 0; attempts < FRAME_RADAR + 1 + MAXRENDER_EMERGENCIES; attempts++)
  {
    _currentFrame++;
    if (_currentFrame > FRAME_EMERGENCY_BASE + emergencyCount - 1 && _currentFrame > FRAME_RADAR)
    {
      _currentFrame = FRAME_OVERVIEW;
    }
    if (isFrameEnabled(_currentFrame))
    {
      return;
    }
  }
  // Nothing enabled — fall back to whatever frame we landed on.
}

unsigned long _runCurrent;
unsigned long _runFrame;
unsigned long _runTime;
unsigned long _runDataUpdate = 0;
unsigned long _runWiFiConnectionCheck = 0;

bool _initComplete = false;

String _ip = "";

#define NTPTIMEOUTVAL 4500
const char *ntpServer = "pool.ntp.org";

const long timezoneOffset = 0; // 0-23
const long gmtOffset_sec = timezoneOffset * 60 * 60;
const int daylightOffset_sec = 0;
unsigned long _epochTime;

String _configJSONURI = "";
int _configFlipSreen = 999;

String _lastMQTTMessage = "";

/***************************************************
  Screen Control Functions
****************************************************/

void setBrightness(byte brightnessValue)
{
  DEBUG_PRINTLN("setBrightness: " + String(brightnessValue));

  display_set_brightness(brightnessValue);

  for (int i = 0; i < 5; i++)
  {
    if (brightnessValue == _brightnesses[i])
    {
      _selectedBrightness = i;
      break;
    }
  }
}

void toggleBrightness(bool isBright)
{
  _brightness = (isBright) ? MAXBRIGHTNESS : MINBRIGHTNESS;
  setBrightness(_brightness);
  _brightnessHigh = isBright;
}

/***************************************************
  SPIFFS functions
****************************************************/
bool parseConfigValue(String key, String value)
{
  DEBUG_PRINTLN("Parsing Config Value, " + key + ": " + value);

  key.toLowerCase();
  value.trim();

  if (key == "jsonuri")
  {
    value.toUpperCase();
    if (_configJSONURI != value)
    {
      _configJSONURI = value;
      _locationCode = _configJSONURI;
      _forceUpdate = true;
    }
  }

  if (key == "flipscreen")
  {
    int __intValue = (value == "true" ? 3 : 1);
    _configFlipSreen = __intValue;
  }

  if (key == "brightness")
  {
    int __newVal = value.toInt();
    if (_brightness != __newVal)
    {
      _brightness = value.toInt();
      setBrightness(_brightness);
    }
  }

  if (key == "mapenabled")
  {
    bool __newEnabled = (value == "true");
    if (radarmap::s_enabled != __newEnabled)
    {
      radarmap::s_enabled = __newEnabled;
      _mapSettingsChanged = true;
    }
  }

  if (key == "mapstyle")
  {
    radarmap::Style __newStyle = radarmap::style_from_name(value);
    if (radarmap::s_style != __newStyle)
    {
      radarmap::s_style = __newStyle;
      _mapSettingsChanged = true;

      // Streets (highlight) renders every road at full brightness — at the
      // 40nmi default range roads merge into a solid mesh that swamps the
      // radar rings/aircraft. Nudge to a range where individual roads and
      // labels stay legible. Only applies switching INTO this style with
      // the range still at its as-shipped default — an explicit maprange
      // set by the user (via config.ini or the webserver, in this request
      // or a prior one) is never overridden.
      if (__newStyle == radarmap::Style::STREETS_HIGHLIGHT && radarmap::s_range_nmi == 40.0f)
      {
        radarmap::s_range_nmi = 12.0f;
      }
    }
  }

  if (key == "maprange")
  {
    float __newRange = value.toFloat();
    if (__newRange > 0 && __newRange != radarmap::s_range_nmi)
    {
      radarmap::s_range_nmi = __newRange;
      _mapSettingsChanged = true;
    }
  }

  DEBUG_PRINTLN("parseConfigValue() - completed...");

  return true;
}

void setupSPIFFS()
{
  if (SPIFFS.begin())
  {
    DEBUG_PRINTLN("SPIFFS: Mounted file system");
  }
  else
  {
    DEBUG_PRINTLN("SPIFFS: FAILED to mount file system!");
  }
}
void loadCustomParamsSPIFFS()
{
  // read configuration from FS json
  DEBUG_PRINTLN("loadCustomParamsSPIFFS() - Open config file...");

  File __configFile = SPIFFS.open("/config.ini", FILE_READ);
  if (__configFile)
  {
    DEBUG_PRINTLN("Reading config file [" + String(__configFile.size()) + " bytes]");
    while (__configFile.available())
    {
      String __inString = __configFile.readStringUntil('\n');
      DEBUG_PRINTLN("Read line: " + __inString);
      int __equalsLoc = __inString.indexOf('=');

      String __key = __inString.substring(0, __equalsLoc);
      String __value = __inString.substring(__equalsLoc + 1, __inString.length());

      parseConfigValue(__key, __value);
    }

    DEBUG_PRINTLN("loadCustomParamsSPIFFS() - close config file...");
    __configFile.close();
    DEBUG_PRINTLN("... Done");
  }
}

void writeStrtoFile(File file, String key, String value)
{
  DEBUG_PRINTLN("    " + key + ": " + value);
  file.println(key + "=" + value);
}

void saveConfigValuesSPIFFS()
{
  DEBUG_PRINTLN("saveConfigValuesSPIFFS()");
  if (SPIFFS.remove("/config.ini"))
  {
    DEBUG_PRINTLN("Deleted old file");
  }

  DEBUG_PRINTLN("Open File in Write Mode");
  // open the file in write mode
  File __configFile = SPIFFS.open("/config.ini", FILE_WRITE);
  DEBUG_PRINTLN("Saving config to FS");

  writeStrtoFile(__configFile, "jsonURI", String(_locationCode));
  writeStrtoFile(__configFile, "flipscreen", String(_configFlipSreen == 3));
  writeStrtoFile(__configFile, "brightness", String(_brightness));
  writeStrtoFile(__configFile, "mapenabled", String(radarmap::s_enabled ? "true" : "false"));
  writeStrtoFile(__configFile, "mapstyle", String(radarmap::style_name(radarmap::s_style)));
  writeStrtoFile(__configFile, "maprange", String(radarmap::s_range_nmi, 1));

  __configFile.close();
  DEBUG_PRINTLN("... Done");
  delay(250); // give SPIFFS chance to settle
}

void DisplayOut(String outStr)
{
  display_debug_line(outStr);
}

/***************************************************
  MQTT
****************************************************/
void mqttTransmitCustomSubscribe() {}
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  DEBUG_PRINT("Message arrived [");
  DEBUG_PRINT(topic);
  DEBUG_PRINT("] ");
  char message_buff[100];
  int i = 0;
  for (i = 0; i < length; i++)
  {
    message_buff[i] = payload[i];
  }
  message_buff[i] = '\0';
  String __payloadString = String(message_buff);

  DEBUG_PRINTLN(__payloadString);

  String __incomingTopic = String(topic);

  _lastMQTTMessage = __incomingTopic + " " + __payloadString;

  if (__incomingTopic == "cmnd/" + String(MQTT_CLIENTNAME) + "/reset")
  {
    DEBUG_PRINTLN("Resetting ESP");
    ESP.restart();
  }
  if (__incomingTopic == "cmnd/" + String(MQTT_CLIENTNAME) + "/info")
  {
    mqttTransmitInitStat();
  }
  if (__incomingTopic == "cmnd/mcmddevices/brightness")
  {
    int __newBrightness = __payloadString.toInt();

    if (__newBrightness < MINBRIGHTNESS)
      __newBrightness = MINBRIGHTNESS;
    if (__newBrightness > MAXBRIGHTNESS)
      __newBrightness = MAXBRIGHTNESS;

    _brightness = __newBrightness;
    DEBUG_PRINTLN("Setting Brightness to: " + String(_brightness));
    setBrightness(_brightness);
  }

  if (__incomingTopic == "cmnd/mcmddevices/brightnesspercentage")
  {
    _brightness = __payloadString.toInt();
    _brightness = map(_brightness, 0, 100, 0, 255);
    DEBUG_PRINTLN("Setting Brightness to: " + String(_brightness));
    setBrightness(_brightness);
  }
}
void mqttCustomSubscribe() {}
void mqttTransmitCustomStat() {}

/***************************************************
  Web Server
****************************************************/
void setupWebServer()
{
  DEBUG_PRINTLN("Handling Web Request...");

  _httpServer.on("/", []()
                 {
					   String __infoStr = "<html><head>"+style;
             __infoStr += "<script>  ";
             __infoStr += "function checkFlipped() {      document.getElementById('flipscreen').value=document.getElementById('flipscreenHidden').checked;  }";
             __infoStr += "function submitForm() { checkFlipped();    document.getElementById('myForm').submit(); }";
             __infoStr +="</script>";
             __infoStr += "</head>";
					   __infoStr += "<div align=left><H1><i>" + String(MQTT_CLIENTNAME) + "</i></H1>";
             __infoStr += loginIndex+loginIndex2;

					   __infoStr += "<hr class='new5'>";
             __infoStr += "<form action='/set' id='myForm'>";


             __infoStr += "Aircraft JSON URL: <input for='jsonURI' data-lpignore='jsonURI' name='jsonURI' type='text' value='"+_locationCode+"' width=80%><br>";
             __infoStr +=  "<br>";

            __infoStr += "Screen brightness:&nbsp;&nbsp;";
            __infoStr += "<select id='brightness' name='brightness'>";
            for (int i = 0; i < 5; i++)
            {
                __infoStr += "<option value='"+String(_brightnesses[i])+"'"+ (_selectedBrightness==i?"selected='selected'":"") +">"+String(map(_brightnesses[i], 0, 255, 0, 100))+"%</option>";
            }

            __infoStr += "</select><br>";
            __infoStr += "<br>";

            __infoStr += "Radar background map:&nbsp;&nbsp;";
            __infoStr += "<select id='mapenabled' name='mapenabled'>";
            __infoStr += "<option value='true'" + String(radarmap::s_enabled ? " selected='selected'" : "") + ">On</option>";
            __infoStr += "<option value='false'" + String(!radarmap::s_enabled ? " selected='selected'" : "") + ">Off</option>";
            __infoStr += "</select><br>";

            __infoStr += "Map style:&nbsp;&nbsp;";
            __infoStr += "<select id='mapstyle' name='mapstyle'>";
            {
              const radarmap::Style __styles[] = {radarmap::Style::DARK_GRAY, radarmap::Style::LIGHT_GRAY, radarmap::Style::STREETS, radarmap::Style::IMAGERY, radarmap::Style::STREETS_HIGHLIGHT};
              const char *__styleLabels[] = {"Dark Gray", "Light Gray", "Streets", "Imagery", "Streets (black bg, highlighted roads)"};
              for (int i = 0; i < 5; i++)
              {
                __infoStr += "<option value='" + String(radarmap::style_name(__styles[i])) + "'" + (radarmap::s_style == __styles[i] ? " selected='selected'" : "") + ">" + __styleLabels[i] + "</option>";
              }
            }
            __infoStr += "</select><br>";

            __infoStr += "Map range (nmi):&nbsp;&nbsp;";
            __infoStr += "<input id='maprange' name='maprange' type='number' min='1' max='250' step='1' value='" + String(radarmap::s_range_nmi, 0) + "'><br>";
            __infoStr += "<br>";

             __infoStr += "<input type='submit' class='btn' value='Save setting(s)'>";
             __infoStr += "</form>";
             __infoStr += "<form action='/refreshmap'>";
             __infoStr += "<input type='submit' class='btn' value='Refresh map now'>";
             __infoStr += "</form>";


					   __infoStr += "<hr  class='new5'>Connected to: " + String(SSID) + " (" + _rssiQualityPercentage + "%)<br>";
					   __infoStr += "Last Message Received:  <i>" + _lastMQTTMessage;
					   __infoStr += "</i><br>Last Message Published: <i>" + _lastPublishedMQTTMessage;

					   __infoStr += "</i><br><hr  class='new5'>IP Address: " + IpAddress2String(WiFi.localIP());
					   __infoStr += "<br>MAC Address: " + WiFi.macAddress();
					   __infoStr += "<br>" + String(MQTT_CLIENTNAME) + " - Firmware version: <b>" + String(MCMDVERSION,1);
					   __infoStr += "</b></div>";

					   String __retStr = __infoStr+"</html>";

					   _httpServer.sendHeader("Connection", "close");
					   _httpServer.send(200, "text/html", __retStr); });

  _httpServer.on("/serverIndex", HTTP_GET, []()
                 {
					   _httpServer.sendHeader("Connection", "close");
					   _httpServer.send(200, "text/html", serverIndex); });

  _httpServer.on("/reset", []()
                 {
					   String _webClientReturnString = "Resetting device";
					   _httpServer.send(200, "text/plain", _webClientReturnString);
					   ESP.restart();
					   delay(1000); });
  _httpServer.on("/resetSettings", []()
                 {
                   String _webClientReturnString = "Resetting Settings";
                   _httpServer.send(200, "text/plain", _webClientReturnString);

                   if (SPIFFS.exists("/config.ini"))
                   {
                     DEBUG_PRINTLN("Removing Configuration files from SPIFFS");
                     SPIFFS.remove("/config.ini");
                   } });

  _httpServer.on("/defaults", []()
                 {
                    String _webClientReturnString = "Resetting device to defaults";
                    _httpServer.send(200, "text/plain", _webClientReturnString); });

  _httpServer.on("/refreshmap", []()
                 {
                   // Respond first — the map fetch below blocks this single-threaded
                   // server for up to ~15s, so the browser needs something to show
                   // (and stop spinning on) before that happens, not after.
                   String __waitPage = "<html><head>" + style;
                   __waitPage += "<meta http-equiv='refresh' content='16;url=/'>";
                   __waitPage += "</head><body><p>Refreshing radar map, this can take up to 15 seconds&hellip;</p>";
                   __waitPage += "<p>You'll be returned to the settings page automatically.</p></body></html>";
                   _httpServer.send(200, "text/html", __waitPage);

                   DisplayOut("Refreshing radar basemap");
                   bool __ok = radarmap::refresh();
                   DEBUG_PRINTLN(__ok ? "Radar map refreshed" : "Radar map refresh failed (check WiFi / basemap enabled)");
                   _forceRender = true; });

  /*handling uploading firmware file */
  _httpServer.on(
      "/update", HTTP_POST, []()
      {
			_httpServer.sendHeader("Connection", "close");
			_httpServer.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
			ESP.restart(); },
      []()
      {
        HTTPUpload &upload = _httpServer.upload();
        if (upload.status == UPLOAD_FILE_START)
        {
          DisplayOut("Updating Firmware");
          DEBUG_PRINT("Update: ");
          DEBUG_PRINTLN(upload.filename.c_str());
          if (!Update.begin(UPDATE_SIZE_UNKNOWN))
          { // start with max available size
            Update.printError(Serial);
          }
        }
        else if (upload.status == UPLOAD_FILE_WRITE)
        {
          /* flashing firmware to ESP*/
          if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
          {
            Update.printError(Serial);
          }
        }
        else if (upload.status == UPLOAD_FILE_END)
        {
          if (Update.end(true))
          { // true to set the size to the current progress
            DEBUG_PRINTLN("Update Success:" + String(upload.totalSize) + "\nRebooting...\n");
          }
          else
          {
            Update.printError(Serial);
          }
        }
      });

  _httpServer.on("/set", HTTP_GET, []()
                 {
			String __retMessage = "";
			String __val = "";
			bool __update = false;
			_mapSettingsChanged = false;

			for (uint8_t i = 0; i < _httpServer.args(); i++) {
				__val = _httpServer.arg(i);
				String __key = _httpServer.argName(i);
				__update = parseConfigValue(__key, __val);
				__retMessage += " " + _httpServer.argName(i) + ": " + _httpServer.arg(i) + (__update ? " set." : " not set.") + "\n";
			}

			if (__update) {
				saveConfigValuesSPIFFS();
			}

			// Send the response — and if it needs a map re-fetch, show a visible
			// wait notice instead of "set." text, since the browser's request
			// won't finish (spinner keeps going) until the blocking refresh
			// below returns and this handler exits.
			if (_mapSettingsChanged) {
				String __waitPage = "<html><head>" + style;
				__waitPage += "<meta http-equiv='refresh' content='16;url=/'>";
				__waitPage += "</head><body><p>Settings saved. Refreshing radar map, this can take up to 15 seconds&hellip;</p>";
				__waitPage += "<p>You'll be returned to the settings page automatically.</p></body></html>";
				_httpServer.send(200, "text/html", __waitPage);
			} else {
				_httpServer.send(200, "text/plain", __retMessage);
			}

			if (_mapSettingsChanged) {
				DisplayOut("Radar map settings changed, refreshing");
				radarmap::refresh();
				_forceRender = true;
				_mapSettingsChanged = false;
			} });

  _httpServer.onNotFound(handleSendToRoot);

  _httpServer.begin();

  DEBUG_PRINTLN("Web Request Completed...");
}

void updateLocalTime()
{
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo))
  {
    return;
  }

  refreshBSTCache(&timeinfo); // recomputes only when the UTC hour changes
  adjustBST(&timeinfo);

  strftime(timeHour, 3, "%H", &timeinfo);
  strftime(timeMin, 3, "%M", &timeinfo);
  strftime(timeSec, 3, "%S", &timeinfo);
}

/***************************************************
  Touch Actions (replace physical button handlers)
****************************************************/

void rebootESP()
{
  DEBUG_PRINTLN("Rebooting ESP");
  delay(250);
  ESP.restart();
}

void toggleSysInfoFrame()
{
  DEBUG_PRINTLN("toggleSysInfoFrame");
  _currentFrame = (_currentFrame == FRAME_SYSINFO ? FRAME_TOPSTATS : FRAME_SYSINFO);
  _forceRender = true;
}

void rotateBrightness()
{
  DEBUG_PRINTLN("rotateBrightness");
  _selectedBrightness--;
  if (_selectedBrightness < 0)
    _selectedBrightness = 4;

  setBrightness(_brightnesses[_selectedBrightness]);
}

void advanceFrame()
{
  DEBUG_PRINTLN("advanceFrame");
  _forceRender = true;
}

void triggerFetchFromButton()
{
  if (_fetchTaskHandle != nullptr && !_fetchInProgress)
  {
    _fetchInProgress = true;
    _runDataUpdate = millis();
    xTaskNotifyGive(_fetchTaskHandle);
  }
}

void onTouchAdvanceFrame() { advanceFrame(); }
void onTouchRotateBrightness() { rotateBrightness(); }
void onTouchTriggerFetch() { triggerFetchFromButton(); }
void onTouchToggleSysInfo() { toggleSysInfoFrame(); }
void onTouchReboot() { rebootESP(); }

/***************************************************
  ADS-B fetch / render orchestration
****************************************************/

void updateFlightStats()
{
  DEBUG_PRINTLN("updateFlightStats");
  if (WiFi.status() == WL_CONNECTED)
  {
    DEBUG_PRINTLN("WiFi Connected");
    DEBUG_PRINTLN("updateFlightStats.fetchFlightData");
    DisplayOut("Fetching flight data");

    DisplayOut("Parsing flight data");
    if (fetchFlightData(host, path, port, _flightDetailsJSONDoc))
    {
      processFlightData(_flightDetailsJSONDoc, _flightStats);
      printFlightStats();
    }
    else
    {
      DEBUG_PRINTLN("Failed to fetch flight data!");
    }
  }
}

void setupWifi()
{
  DisplayOut("Initialising WiFi: 1st AP");
  DisplayOut(_networkConnection ? WIFI_ACCESSPOINT : WIFI_ACCESSPOINT1);

  if (!isWiFiConnected(_mqttClientId))
  {
    flipAPDetails();
    DisplayOut("Initialising WiFi: 2nd AP");
    DisplayOut(_networkConnection ? WIFI_ACCESSPOINT : WIFI_ACCESSPOINT1);

    if (!isWiFiConnected(_mqttClientId))
    {
      DisplayOut("WiFi connection failed");
      DisplayOut("Restarting device");
      rebootESP();
    }
  }
}

// FreeRTOS task — runs on core 1 (WiFiClient affinity). Sleeps until notified
// by the main loop, then fetches + processes flight data into the staging
// struct, swaps it into _flightStats under mutex, then sleeps again.
void fetchTask(void *pvParameters)
{
  for (;;)
  {
    // Block indefinitely until xTaskNotifyGive() is called from the main loop.
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    if (WiFi.status() != WL_CONNECTED)
    {
      DEBUG_PRINTLN("fetchTask: WiFi not connected, skipping");
      _fetchInProgress = false;
      continue;
    }

    DEBUG_PRINTLN("fetchTask: fetching flight data");

    if (fetchFlightData(host, path, port, _flightDetailsJSONDoc))
    {
      processFlightData(_flightDetailsJSONDoc, _flightStatsStaging);

      // Swap staging into live struct under mutex so the render path
      // on core 1 never sees a partially-updated _flightStats.
      xSemaphoreTake(_flightStatsMutex, portMAX_DELAY);
      _flightStats = _flightStatsStaging;
      xSemaphoreGive(_flightStatsMutex);

      _spritesNeedUpdate = true; // signal loop() to re-render from new data
      DEBUG_PRINTLN("fetchTask: data updated");
    }
    else
    {
      DEBUG_PRINTLN("fetchTask: fetchFlightData failed, keeping previous data");
    }

    _fetchInProgress = false;
  }
}

void setup()
{
  Serial.begin(115200);

  DEBUG_PRINTLN("Starting...");
  _mqttPostFix = String(random(0xffff), HEX);
  _mqttClientId = MQTT_CLIENTNAME;
  _deviceClientName = MQTT_CLIENTNAME;

  display_init();
  delay(250); // give the screen time to init

  DisplayOut("Starting ADSBMonitor");
  DisplayOut("----------------------------------");

  setupWifi();

  DisplayOut("Web Server config");
  setupWebServer();

  DisplayOut("Web Server starting");
  _httpServer.begin();

  DisplayOut("OTA Firmware Setup");
  setupOTA();

  DisplayOut("DNS Setup");
  if (MDNS.begin(_deviceClientName))
  {
    DisplayOut("Connect to:");
    DisplayOut(" http://" + String(_deviceClientName) + ".local");
    MDNS.addService("http", "tcp", 80);
  }
  else
  {
    DisplayOut("DNS Setup failed");
  }
  _ip = WiFi.localIP().toString();
  DisplayOut("IP:" + _ip);

  DisplayOut("Configuring MQTT");
  setupMQTT();
  mqttReconnect(_mqttClientId);
  mqttSendInitStat();

  DisplayOut("Opening Filesystem");
  setupSPIFFS();
  loadCustomParamsSPIFFS();

  DisplayOut("Loading radar basemap");
  radarmap::init();

  DisplayOut("Updating local time");
  setupTimeClient();
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  updateLocalTime();
  DisplayOut("Check BST");
  checkBST();

  // Create the mutex that guards _flightStats between core 1 (fetch) and core 1 (render).
  _flightStatsMutex = xSemaphoreCreateMutex();
  if (_flightStatsMutex == nullptr)
  {
    Serial.println("FATAL: failed to create _flightStatsMutex");
    esp_restart();
  }

  // Create the fetch task on core 1. WiFiClient must run on core 1 — the
  // Arduino WiFi/lwip API is designed for core 1 and relies on the IDLE
  // task there for cooperative yielding. Running it on core 0 starves
  // the core 0 IDLE task and triggers the task watchdog.
  xTaskCreatePinnedToCore(
    fetchTask,        // task function
    "fetchTask",      // name (debug)
    8192,             // stack bytes
    nullptr,          // parameter
    1,                // priority (same as loop task)
    &_fetchTaskHandle,// handle — used by loop to notify
    1                 // core 1 — same core as loop(), required for WiFiClient
  );

  DisplayOut("Free Heap Memory: " + String(ESP.getFreeHeap()));
  DisplayOut("Initialisation complete");
  _forceUpdate = true;
  _forceRender = true;
  _initComplete = true;
}

void loop()
{
  lv_timer_handler();

  if (_initComplete && !_updatingFirmware)
  {
    _runCurrent = millis(); // sets the counter

    // Poll ADS-B data faster while showing the radar screen so aircraft
    // motion is visible on the sweep; normal cadence otherwise.
    unsigned long adsbInterval = (_currentFrame == FRAME_RADAR)
        ? UPDATE_ADSBS_INTERVAL_RADAR_MILLISECS
        : UPDATE_ADSBS_INTERVAL_MILLISECS;

    if (_runCurrent - _runDataUpdate >= adsbInterval || _forceUpdate)
    {
      if (!_fetchInProgress)
      {
        _fetchInProgress = true;
        _runDataUpdate = millis();
        xTaskNotifyGive(_fetchTaskHandle);
      }
      _forceUpdate = false;
    }

    if (_spritesNeedUpdate)
    {
      _spritesNeedUpdate = false;
      _forceRender = true; // re-render current frame with the new data
    }

    if (_runCurrent - _runWiFiConnectionCheck >= UPDATE_WIFICHECK_INTERVAL_MILLISECS)
    {
      isWiFiConnected(); // make sure we're still connected
      _runWiFiConnectionCheck = millis();

      if (!_mqttClient.connected())
      {
        mqttReconnect();
      }
    }

    if ((_runCurrent - _runFrame >= UPDATE_UI_FRAME_INTERVAL_MILLISECS) || _forceUpdate || _forceRender)
    {
      DEBUG_PRINTLN("Current Frame: " + String(_currentFrame));

      xSemaphoreTake(_flightStatsMutex, portMAX_DELAY);

      display_render_frame(_currentFrame, _flightStats);

      if (_flightStats.totalAircraft > 0)
      {
        advanceToNextEnabledFrame(_flightStats.emergencyCount);
      }

      xSemaphoreGive(_flightStatsMutex);

      _forceUpdate = false;
      _forceRender = false;
      _runFrame = millis();
    }

    if (_runCurrent - _runTime >= UPDATE_TIME_INTERVAL_MILLISECS)
    {
      updateLocalTime();
      display_update_clock();
      _runTime = millis();
    }
  }

  _mqttClient.loop();
  ArduinoOTA.handle(); /* this function will handle incomming chunk of SW, flash and respond sender */
  _httpServer.handleClient(); //// Check if a client has connected
}
