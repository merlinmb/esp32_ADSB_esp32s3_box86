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

#define location "51.39502, -1.3387" // 97 Enborne Road

#define MCMDVERSION 1.5

bool _brightnessHigh = true;
byte _brightness = MAXBRIGHTNESS;

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
#define UPDATE_UI_RADAR_FRAME_INTERVAL_MILLISECS UPDATE_UI_FRAME_INTERVAL_MILLISECS
#define UPDATE_ADSBS_INTERVAL_MILLISECS 30000     // Update every 30 seconds
#define UPDATE_TIME_INTERVAL_MILLISECS 900        // Update every <1sec
unsigned long _radarDwellMillis = 60000;

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
  _brightnessHigh = brightnessValue > MINBRIGHTNESS;
  _brightness = _brightnessHigh ? MAXBRIGHTNESS : MINBRIGHTNESS;
  DEBUG_PRINTLN("setBrightness: " + String(_brightness));

  display_set_brightness(_brightness);
}

void toggleBrightness(bool isBright)
{
  setBrightness(isBright ? MAXBRIGHTNESS : MINBRIGHTNESS);
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
    bool __newBrightnessHigh = value.toInt() > MINBRIGHTNESS;
    if (_brightnessHigh != __newBrightnessHigh)
    {
      toggleBrightness(__newBrightnessHigh);
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

  if (key == "scanline")
  {
    radarmap::s_scan_line_enabled = (value == "true");
  }

  if (key == "nearestinfo")
  {
    radarmap::s_always_show_nearest_info = (value == "true");
  }

  if (key == "simulateemergency")
  {
    radarmap::s_simulate_emergency = (value == "true");
  }

  if (key == "radardwellseconds")
  {
    unsigned long __seconds = value.toInt();
    if (__seconds < 60) __seconds = 60;
    _radarDwellMillis = __seconds * 1000UL;
  }

  DEBUG_PRINTLN("parseConfigValue() - completed...");

  return true;
}

void setupSPIFFS()
{
  if (SPIFFS.begin(true))
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
  writeStrtoFile(__configFile, "scanline", String(radarmap::s_scan_line_enabled ? "true" : "false"));
  writeStrtoFile(__configFile, "nearestinfo", String(radarmap::s_always_show_nearest_info ? "true" : "false"));
  writeStrtoFile(__configFile, "radardwellseconds", String(_radarDwellMillis / 1000UL));

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

    toggleBrightness(__newBrightness > MINBRIGHTNESS);
  }

  if (__incomingTopic == "cmnd/mcmddevices/brightnesspercentage")
  {
    toggleBrightness(__payloadString.toInt() > 0);
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
             String __infoStr = "<html><head><meta name='viewport' content='width=device-width,initial-scale=1'>" + style;
             __infoStr += "<script>function syncToggles(){['brightness','mapenabled','scanline','nearestinfo','simulateemergency'].forEach(function(id){document.getElementById(id+'Value').value=document.getElementById(id).checked?'true':'false';});document.getElementById('brightnessValue').value=document.getElementById('brightness').checked?'255':'0';}</script></head><body><main class='shell'><header class='masthead'><div class='brand'>FLIGHT RADAR<small>DEVICE CONFIGURATION</small></div><div class='badge'>ONLINE</div></header>";
             __infoStr += "<form action='/set' id='myForm' onsubmit='syncToggles()'>";
             __infoStr += "<section class='panel'><h2>DATA SOURCE</h2><div class='field'><label for='jsonURI'>Aircraft JSON URL</label><input id='jsonURI' data-lpignore='true' name='jsonURI' type='text' value='" + _locationCode + "'></div></section>";
             __infoStr += "<section class='panel'><h2>DISPLAY</h2><input type='hidden' id='brightnessValue' name='brightness'><label class='toggle'><span>Screen backlight</span><input id='brightness' type='checkbox'" + String(_brightnessHigh ? " checked" : "") + "><i class='switch'></i></label><input type='hidden' id='scanlineValue' name='scanline'><label class='toggle'><span>Radar scanning line</span><input id='scanline' type='checkbox'" + String(radarmap::s_scan_line_enabled ? " checked" : "") + "><i class='switch'></i></label><input type='hidden' id='nearestinfoValue' name='nearestinfo'><label class='toggle'><span>Always show nearest aircraft info</span><input id='nearestinfo' type='checkbox'" + String(radarmap::s_always_show_nearest_info ? " checked" : "") + "><i class='switch'></i></label><div class='field'><label for='radardwellseconds'>Radar dwell (seconds)</label><input id='radardwellseconds' name='radardwellseconds' type='number' min='60' step='1' value='" + String(_radarDwellMillis / 1000UL) + "'></div></section>";
             __infoStr += "<section class='panel'><h2>RADAR MAP</h2><input type='hidden' id='mapenabledValue' name='mapenabled'><label class='toggle'><span>Background map</span><input id='mapenabled' type='checkbox'" + String(radarmap::s_enabled ? " checked" : "") + "><i class='switch'></i></label><div class='field'><label for='mapstyle'>Map style</label><select id='mapstyle' name='mapstyle'>";
            {
              const radarmap::Style __styles[] = {radarmap::Style::DARK_GRAY, radarmap::Style::LIGHT_GRAY, radarmap::Style::STREETS, radarmap::Style::IMAGERY, radarmap::Style::STREETS_HIGHLIGHT};
              const char *__styleLabels[] = {"Dark Gray", "Light Gray", "Streets", "Imagery", "Streets (black bg, highlighted roads)"};
              for (int i = 0; i < 5; i++)
              {
                __infoStr += "<option value='" + String(radarmap::style_name(__styles[i])) + "'" + (radarmap::s_style == __styles[i] ? " selected='selected'" : "") + ">" + __styleLabels[i] + "</option>";
              }
            }
            __infoStr += "</select></div><div class='field'><label for='maprange'>Map range (nmi)</label>";
            __infoStr += "<input id='maprange' name='maprange' type='number' min='1' max='250' step='1' value='" + String(radarmap::s_range_nmi, 0) + "'></div></section>";
            __infoStr += "<section class='panel'><h2>DEBUG</h2><input type='hidden' id='simulateemergencyValue' name='simulateemergency'><label class='toggle'><span>Simulate emergency aircraft</span><input id='simulateemergency' type='checkbox'" + String(radarmap::s_simulate_emergency ? " checked" : "") + "><i class='switch'></i></label></section>";
            __infoStr += "<div class='actions'><input type='submit' class='btn' value='Save settings'></div></form>";
            __infoStr += "<form action='/refreshmap' class='actions'><input type='submit' class='btn secondary' value='Refresh map'></form>";
            __infoStr += "<section class='panel'><h2>CONNECTION</h2><div class='status'>";
            __infoStr += "<div><span>WI-FI</span><strong>" + String(SSID) + " (" + _rssiQualityPercentage + "%)</strong></div>";
            __infoStr += "<div><span>IP ADDRESS</span><strong>" + IpAddress2String(WiFi.localIP()) + "</strong></div>";
            __infoStr += "<div><span>MAC ADDRESS</span><strong>" + WiFi.macAddress() + "</strong></div>";
            __infoStr += "<div><span>FIRMWARE</span><strong>" + String(MQTT_CLIENTNAME) + " " + String(MCMDVERSION, 1) + "</strong></div>";
            __infoStr += "<div><span>LAST MESSAGE RECEIVED</span><strong>" + _lastMQTTMessage + "</strong></div>";
            __infoStr += "<div><span>LAST MESSAGE PUBLISHED</span><strong>" + _lastPublishedMQTTMessage + "</strong></div></div></section></main></body>";

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
  toggleBrightness(!_brightnessHigh);
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
void onTouchToggleInfoOverlay() { display_toggle_info_overlay(); }
void onTouchReboot() { rebootESP(); }

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

      // Debug/demo toggle (webserver): force the closest aircraft to read as
      // an emergency so the emergency UI can be previewed without a real one.
      if (radarmap::s_simulate_emergency && _flightStatsStaging.emergencyCount == 0 &&
          _flightStatsStaging.totalAircraft > 0)
      {
        int __simIndex = _flightStatsStaging.closestAircraft;
        _flightStatsStaging.aircraft[__simIndex].squawk = 7700;
        _flightStatsStaging.emergencyAircraft[_flightStatsStaging.emergencyCount++] = __simIndex;
      }

      xSemaphoreTake(_flightStatsMutex, portMAX_DELAY);
      _flightStats = _flightStatsStaging;
      xSemaphoreGive(_flightStatsMutex);

      _spritesNeedUpdate = true;
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
  //
  // Priority is BELOW loopTask (1) so a blocking fetch (TCP/TLS/JSON parse,
  // several seconds) never preempts LVGL rendering or touch I2C polling —
  // it only runs when loopTask yields/blocks. Equal priority previously
  // caused round-robin time-slicing with loopTask, stalling lv_timer_handler()
  // and I2C mid-transaction (visible as RGB panel shift + I2C bus errors).
  xTaskCreatePinnedToCore(
    fetchTask,        // task function
    "fetchTask",      // name (debug)
    8192,             // stack bytes
    nullptr,          // parameter
    0,                // priority — below loopTask, never preempts rendering
    &_fetchTaskHandle,// handle — used by loop to notify
    1                 // core 1 — same core as loop(), required for WiFiClient
  );

  DisplayOut("Sending initial MQTT status");
  mqttSendInitStat();

  DisplayOut("Free Heap Memory: " + String(ESP.getFreeHeap()));
  DisplayOut("Initialisation complete");
  _forceUpdate = true;
  _forceRender = true;
  _initComplete = true;
}

void loop()
{
  lv_timer_handler();
  delay(2);

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

    bool dataUpdated = _spritesNeedUpdate;
    if (dataUpdated)
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

    unsigned long frameInterval = (_currentFrame == FRAME_RADAR)
      ? _radarDwellMillis
      : UPDATE_UI_FRAME_INTERVAL_MILLISECS;

    bool frameElapsed = _runCurrent - _runFrame >= frameInterval;
    if (frameElapsed || _forceUpdate || _forceRender)
    {
      DEBUG_PRINTLN("Current Frame: " + String(_currentFrame));

      xSemaphoreTake(_flightStatsMutex, portMAX_DELAY);

      display_render_frame(_currentFrame, _flightStats);

        if (_flightStats.totalAircraft > 0 &&
          (_currentFrame != FRAME_RADAR || frameElapsed || !dataUpdated))
      {
        advanceToNextEnabledFrame(_flightStats.emergencyCount);
      }

      xSemaphoreGive(_flightStatsMutex);

      _forceUpdate = false;
      _forceRender = false;
      // Fresh radar data redraws the existing canvas but must not shorten its
      // dwell period. User-forced renders keep their existing advance behavior.
      if (frameElapsed || !dataUpdated)
      {
        _runFrame = millis();
      }
    }

    if (_runCurrent - _runTime >= UPDATE_TIME_INTERVAL_MILLISECS)
    {
      updateLocalTime();
      char clockText[10];
      snprintf(clockText, sizeof(clockText), "%s:%s:%s", timeHour, timeMin, timeSec);
      display_update_clock(clockText);
      _runTime = millis();
    }
  }

  _mqttClient.loop();
  ArduinoOTA.handle(); /* this function will handle incomming chunk of SW, flash and respond sender */
  _httpServer.handleClient(); //// Check if a client has connected
}
