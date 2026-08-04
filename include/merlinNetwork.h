#pragma once
#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>

#endif

#ifdef ESP32
#include <WiFi.h>
#include <ESPmDNS.h>
#endif

#ifdef SECURE
#include <WiFiClientSecure.h>
#endif

#include <WiFiUdp.h>
//#include <ArduinoOTA.h>

#include <PubSubClient.h>

#include <NTPClient.h>
#include <TimeLib.h>
#include <Time.h>

//#define DEBUG 1
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

#ifndef MQTT_MAX_PACKET_SIZE
#define MQTT_MAX_PACKET_SIZE 512
#endif

byte _networkConnection = 1;

#define MQTT_SERVER_IP "192.168.1.55"
#define MQTT_SERVER_PORT 1883
#define MQTT_FRIENDLYNAME "mcmddevices"

#define OTA_UPDATE_WEBPATH "/firmware"
#define HTTPRESPONSETIMEOUT 450

const char *_deviceClientName = "espTestDevice";

#ifdef SECURE
WiFiClientSecure _espWiFiClient;
#else
WiFiClient _espWiFiClient;
#endif

PubSubClient _mqttClient(_espWiFiClient);
String _mqttClientId = "";
String _lastReceivedMQTTMessage = "";
String _lastPublishedMQTTMessage = "";

int _mqttConnectionRetries = 0;

byte _ledBuiltIn = 12;

// You can specify the time server pool and the offset (in seconds, can be
// changed later with setTimeOffset() ). Additionaly you can specify the
// update interval (in milliseconds, can be changed using setUpdateInterval() ).
WiFiUDP ntpUDP;
// const byte offset = 2;
#define OFFSET 1
const char* _timeServer = "pool.ntp.org"; //"162.159.200.1"; //
NTPClient _timeClient(ntpUDP, _timeServer, OFFSET, 60000);
unsigned long _unixTime;
String _ntpDate = "";
char timeHour[3];
char timeMin[3];
char timeSec[3];
char timeday[3];
char timemonth[10];
char timeyear[5];
char timeWeekDay[10];
int dayInWeek;


// Measure Signal Strength (RSSI) of Wi-Fi connection
long _rssi = 0;
// Measure Signal Strength (RSSI) of Wi-Fi connection
long _rssiQualityPercentage = 0;

const int RSSI_MAX = -50;  // define maximum strength of signal in dBm
const int RSSI_MIN = -100; // define minimum strength of signal in dBm

// void ICACHE_RAM_ATTR AS3935Irq();
int dBmtoPercentage(int dBm)
{
	int quality = 2 * (dBm + 100);

	if (dBm <= RSSI_MIN)
	{
		quality = 0;
	}
	else if (dBm >= RSSI_MAX)
	{
		quality = 100;
	}

	return quality;
} // dBmtoPercentage

String macToStr(const uint8_t *mac)
{
	String result;
	for (int i = 0; i < 6; ++i)
	{
		result += String(mac[i], 16);
	}
	return result;
}

void updateTimeString()
{
	time_t timestamp = _timeClient.getEpochTime();
	char dateBuffer[17];
	snprintf(dateBuffer, sizeof(dateBuffer), "%04d-%02d-%02d %02d:%02d",
			 year(timestamp), month(timestamp), day(timestamp), hour(timestamp), minute(timestamp));
	_ntpDate = dateBuffer;
	DEBUG_PRINTLN(_ntpDate);
}

String getTimefromEpoch(long int timeData)
{

	time_t t = timeData;
	// time_t Z = 1515675789; //linux epoch time
	/*
	printDigits(day(t));
	tft.print("/");
	printDigits(month(t));
	tft.print("/");
	tft.print(year(t));
	tft.print(" ");
	*/
	byte __hour = (hour(t) + OFFSET) >= 24 ? hour(t) + OFFSET - 24 : hour(t) + OFFSET;
	return String(__hour) + ":" + (minute(t) < 10 ? "0" : "") + String(minute(t));
	;
}

unsigned long webUnixTime(Client &client)
{
	unsigned long time = 0;

	// Just choose any reasonably busy web server, the load is really low
	if (client.connect("g.cn", 80))
	{
		// Make an HTTP 1.1 request which is missing a Host: header
		// compliant servers are required to answer with an error that includes
		// a Date: header.
		client.print(F("GET / HTTP/1.1 \r\n\r\n"));

		char buf[5]; // temporary buffer for characters
		client.setTimeout(500);
		if (client.find((char *)"\r\nDate: ") // look for Date: header
			&& client.readBytes(buf, 5) == 5) // discard
		{
			unsigned day = client.parseInt(); // day
			client.readBytes(buf, 1);		  // discard
			client.readBytes(buf, 3);		  // month
			int year = client.parseInt();	  // year
			byte hour = client.parseInt();	  // hour
			byte minute = client.parseInt();  // minute
			byte second = client.parseInt();  // second

			int daysInPrevMonths;
			switch (buf[0])
			{
			case 'F':
				daysInPrevMonths = 31;
				break; // Feb
			case 'S':
				daysInPrevMonths = 243;
				break; // Sep
			case 'O':
				daysInPrevMonths = 273;
				break; // Oct
			case 'N':
				daysInPrevMonths = 304;
				break; // Nov
			case 'D':
				daysInPrevMonths = 334;
				break; // Dec
			default:
				if (buf[0] == 'J' && buf[1] == 'a')
					daysInPrevMonths = 0; // Jan
				else if (buf[0] == 'A' && buf[1] == 'p')
					daysInPrevMonths = 90; // Apr
				else
					switch (buf[2])
					{
					case 'r':
						daysInPrevMonths = 59;
						break; // Mar
					case 'y':
						daysInPrevMonths = 120;
						break; // May
					case 'n':
						daysInPrevMonths = 151;
						break; // Jun
					case 'l':
						daysInPrevMonths = 181;
						break; // Jul
					default:   // add a default label here to avoid compiler warning
					case 'g':
						daysInPrevMonths = 212;
						break; // Aug
					}
			}

			// This code will not work after February 2100
			// because it does not account for 2100 not being a leap year and because
			// we use the day variable as accumulator, which would overflow in 2149
			day += (year - 1970) * 365; // days from 1970 to the whole past year
			day += (year - 1969) >> 2;	// plus one day per leap year
			day += daysInPrevMonths;	// plus days for previous months this year
			if (daysInPrevMonths >= 59	// if we are past February
				&& ((year & 3) == 0))	// and this is a leap year
				day += 1;				// add one day
										// Remove today, add hours, minutes and seconds this month
			time = (((day - 1ul) * 24 + hour) * 60 + minute) * 60 + second;
		}
	}
	// delay(10);
	client.flush();
	client.stop();

	return time;
}

void getTimeUpdate()
{
	while (!_timeClient.forceUpdate())
	{
		DEBUG_PRINTLN("Fetching Time Update");
		delay(350);
	}
	DEBUG_PRINTLN(_timeClient.getFormattedTime());

	_unixTime = webUnixTime(_espWiFiClient);

	setTime(_unixTime);
}

void mqttCallback(char *topic, byte *payload, unsigned int length);
void mqttTransmitCustomStat();

void mqttTransmitCustomSubscribe();

void RenderHeadingtoSprite();

String IpAddress2String(const IPAddress &ipAddress)
{
	return String(ipAddress[0]) + String(".") +
		   String(ipAddress[1]) + String(".") +
		   String(ipAddress[2]) + String(".") +
		   String(ipAddress[3]);
}

void mqttSubscribe(String _topic)
{
	DEBUG_PRINTLN("Subscribing to: " + _topic);
	_mqttClient.subscribe(_topic.c_str());
	delay(50);
}

void mqttCustomPublish(String topic, String topicPostfix, String value, bool retain)
{

	DEBUG_PRINTLN("mqttCustom: Sending ");
	String __ret = (topic + "/" + topicPostfix);
	String __retVal = value;
	DEBUG_PRINTLN(__ret + " " + __retVal);
	_mqttClient.publish(__ret.c_str(), __retVal.c_str(), retain);
	_lastPublishedMQTTMessage = __ret + " " + __retVal;
	DEBUG_PRINTLN("mqttCustom: Sent");
}

void mqttPublishStat(String deviceName, String topicPostFix, String value, bool retain)
{	
	String __ret = ("stat/" + deviceName + "/" + topicPostFix);
	String __retVal = value;
	
	_mqttClient.publish(__ret.c_str(), __retVal.c_str(), retain);
	_lastPublishedMQTTMessage = __ret + " " + __retVal;
	DEBUG_PRINT("MQTT Sent: ");
	DEBUG_PRINTLN(_lastPublishedMQTTMessage);
}

void mqttPublishStat(String name, String value, bool retain)
{
	mqttPublishStat(_deviceClientName, name, value, retain);
}

void mqttPublishStat(String deviceName, String name, String value)
{
	mqttPublishStat(deviceName, name, value, false);
}
void mqttPublishStat(String name, String value)
{
	mqttPublishStat(_deviceClientName, name, value);
}

void mqttHomePublish(String name, String value, bool retain)
{
	//DEBUG_PRINTLN("MQTT Sending ");
	String __ret = ("stat/home/" + name);
	DEBUG_PRINTLN(__ret + " " + value);
	_mqttClient.publish(__ret.c_str(), value.c_str(), retain);
	_lastPublishedMQTTMessage = __ret + " " + value;
	//DEBUG_PRINTLN("MQTT: Sent");
}

void mqttHomePublish(String name, String value)
{
	mqttHomePublish(name, value, false);
}

void mqttTransmitInitStat(String deviceName)
{
	mqttPublishStat("init", "{\"value1\":\"" + IpAddress2String(WiFi.localIP()) + "\",\"value2\":\"" + WiFi.macAddress() + "\",\"value3\":\"" + deviceName + "\"}");
}

void mqttTransmitInitStat()
{
	mqttTransmitInitStat(_deviceClientName);
}

void tickLED()
{
	// toggle state
	int state = digitalRead(_ledBuiltIn); // get the current state of GPIO1 pin
	if (WiFi.status() == WL_CONNECTED)	  // only switch LED if Wifi is connected
	{
		digitalWrite(_ledBuiltIn, !state); // set pin to the opposite state
	}
	else
	{
		// Turn the LED on (Note that LOW is the voltage level
		// but actually the LED is on; this is because
		// it is acive low on the ESP-01)
		digitalWrite(_ledBuiltIn, HIGH); // Turn the LED off by making the voltage HIGH
	}
}

void mqttHandleFriendlyCallback(String deviceName, String mqttIncomingTopic, String mqttIncomingPayload)
{
	if (mqttIncomingTopic == "cmnd/" + String(MQTT_FRIENDLYNAME) + "/sendstat")
	{
		mqttTransmitInitStat(deviceName);
		return;
	}
	if (mqttIncomingTopic == "cmnd/" + String(MQTT_FRIENDLYNAME) + "/reset")
	{
		mqttPublishStat(deviceName, "restart", mqttIncomingPayload);
		delay(1000);
#ifdef ESP8266
		ESP.reset();
#endif
#ifdef ESP32
		ESP.restart();
#endif
	}
}
void mqttHandleFriendlyCallback(String mqttIncomingTopic, String mqttIncomingPayload)
{
	mqttHandleFriendlyCallback(_deviceClientName, mqttIncomingTopic, mqttIncomingPayload);
}

bool mqttReconnect(String deviceName)
{
	_mqttClientId = deviceName;
	_mqttClientId += "_" + String(random(0xffff), HEX);
	DEBUG_PRINTLN("MQTT reconnecting with client id [" + _mqttClientId + "]");
	int __retryCount = 0;
	_mqttClient.setSocketTimeout(500);
	while (!_mqttClient.connected() && __retryCount < 3)
	{
		DEBUG_PRINTLN("MQTT: Connection Attempt [" + String(__retryCount) + "]");
		if (_mqttClient.connect(_mqttClientId.c_str()))
		{
			DEBUG_PRINTLN("MQTT: Connected");

			DEBUG_PRINTLN("MQTT: Setting callback");
			_mqttClient.setCallback(mqttCallback);

			DEBUG_PRINTLN("MQTT: subscribing...");
			// Once connected, publish an announcement...
			String __outMessage = "tele/" + String(_deviceClientName) + "/alive";
			_mqttClient.publish(__outMessage.c_str(), "1");
			__outMessage = "tele/" + String(_deviceClientName) + "/ip";
			_mqttClient.publish(__outMessage.c_str(), IpAddress2String(WiFi.localIP()).c_str());

			mqttSubscribe("cmnd/" + deviceName + "/#");
			mqttSubscribe("cmnd/" + String(MQTT_FRIENDLYNAME) + "/#");
			//mqttSubscribe("stat/" + deviceName + "/#");

			mqttTransmitInitStat(deviceName);
			mqttTransmitCustomStat();
			mqttTransmitCustomSubscribe();
		}
		else
		{
			DEBUG_PRINT("failed, rc=");
			DEBUG_PRINT(_mqttClient.state());
			DEBUG_PRINTLN(" trying again in a few seconds");

			//_mqttClient.disconnect();
			// Wait a few seconds before retrying
			delay(1500);
			__retryCount++;
		}
	}

	if (__retryCount >= 3)
	{
		// DEBUG_PRINTLN("Forcing WiFi disconnect");
		// WiFi.disconnect();
	}

	return _mqttClient.connected();
}

bool mqttReconnect()
{
	return mqttReconnect(_deviceClientName);
}

void flipAPDetails()
{
	_networkConnection = !_networkConnection;
}

int isWiFiConnected(String deviceName, String apName, String apPassword)
{
	DEBUG_PRINTLN("Checking that the WiFi is connected");

	if (WiFi.status() == WL_CONNECTED)
	{
		DEBUG_PRINT("WiFi connected to '" + apName + "' with IP address: ");
		DEBUG_PRINTLN(WiFi.localIP());
		return true;
	}

	DEBUG_PRINTLN("\r\nConnecting to: " + String(apName));
	WiFi.begin(apName.c_str(), apPassword.c_str());

	byte counter = 0;
	while (WiFi.status() != WL_CONNECTED)
	{
		DEBUG_PRINT(F("."));
		counter++;

		if (counter == 10)
		{
			// ESP.restart();
			DEBUG_PRINTLN();
			return 0;
		}
		delay(1000);
	}

	DEBUG_PRINTLN();
	DEBUG_PRINTLN("WiFi connected\r\n");

	// Print network info
	DEBUG_PRINT("IP: "); DEBUG_PRINTLN(WiFi.localIP().toString());
	DEBUG_PRINT("Gateway: "); DEBUG_PRINTLN(WiFi.gatewayIP().toString());
	DEBUG_PRINT("Subnet: "); DEBUG_PRINTLN(WiFi.subnetMask().toString());

	#ifdef ESP32
		DEBUG_PRINT("DNS: "); DEBUG_PRINTLN(WiFi.dnsIP().toString());
	#else
		DEBUG_PRINT("DNS0: "); DEBUG_PRINTLN(WiFi.dnsIP(0).toString());
		DEBUG_PRINT("DNS1: "); DEBUG_PRINTLN(WiFi.dnsIP(1).toString());
	#endif

	// Try resolving a known hostname
	IPAddress resolved;
	if (WiFi.hostByName("pool.ntp.org", resolved)) {
		DEBUG_PRINTLN("Resolved pool.ntp.org -> " + resolved.toString());
	} else {
		DEBUG_PRINTLN("DNS lookup failed for pool.ntp.org");
	}

	_rssi = WiFi.RSSI();
	DEBUG_PRINT(_rssi); // Signal strength in dBm
	DEBUG_PRINT(" dBm (");

	_rssiQualityPercentage = dBmtoPercentage(_rssi);

	DEBUG_PRINT(_rssiQualityPercentage); // Signal strength in %
	DEBUG_PRINTLN("%)");

	return 1;
}

int isWiFiConnected(String deviceName)
{
	String __apName = _networkConnection ? WIFI_ACCESSPOINT : WIFI_ACCESSPOINT1;
	String __apPassword = _networkConnection ? WIFI_ACCESSPOINT_PASSWORD : WIFI_ACCESSPOINT_PASSWORD1;

	return isWiFiConnected(deviceName, __apName, __apPassword);
}

int isWiFiConnected()
{
	return isWiFiConnected(_deviceClientName);
}

String getWiFIAPName()
{
	return (_networkConnection ? WIFI_ACCESSPOINT : WIFI_ACCESSPOINT1);
}

bool isLeapYear(int year)
{
	return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int zellersCongruence(int year, int month, int day) {
  if (month <= 2) {
    month += 12;
    year--;
  }
  
  int k = year % 100;
  int j = year / 100;
  
  int dayOfWeek = (day + 13*(month + 1)/5 + k + k/4 + j/4 + 5*j ) % 7 ;
  
  return dayOfWeek ; // Convert to Arduino's date format (Sat = 1, Sunday = 1, ...)
}

#ifndef LEAP_YEAR
#define LEAP_YEAR(Y)     ( (Y>0) && !(Y%4) && ( (Y%100) || !(Y%400) ))     // from time-lib
#endif

int calcDayOfWeek(uint16_t year, uint8_t month, uint8_t day)
{
  uint16_t months[] = 
  {
    0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 // days until 1st of month
  };   

  uint32_t days = year * 365;   // days until year 
  for (uint16_t i = 4; i < year; i+=4) 
  {
    if (LEAP_YEAR(i)) days++;  // adjust leap years
  }

  days += months[month-1] + day;
  if ((month > 2) && LEAP_YEAR(year)) days++;
  return days % 7;
}

int lastSunday(int month, int year)
{
	int lastDay = 31; // Initialize lastDay with a default value

	// Adjust lastDay for months with less than 31 days
	if (month == 4 || month == 6 || month == 9 || month == 11)
	{
		lastDay = 30;
	}
	else if (month == 2)
	{
		lastDay = (isLeapYear(year) ? 29 : 28);
	}
	
	
	for (int day = lastDay; day > 0; day--) {
    	//if (zellersCongruence(year, month, day) == 1) 
		if(calcDayOfWeek(year, month, day)==1)
		{
			return day;
		}
	}
	

	return lastDay;
}

// Cached BST state — recomputed once per hour, not every second tick.
static bool _isBSTActive = false;
static int  _isBSTCachedHour = -1;   // UTC hour when cache was last set
static int  _isBSTCachedDay  = -1;   // UTC day when cache was last set

bool isBST(struct tm *timeinfo)
{
	int year       = 1900 + timeinfo->tm_year;
	int month      = timeinfo->tm_mon + 1; // 1-12
	int dayOfMonth = timeinfo->tm_mday;
	int hour       = timeinfo->tm_hour;    // UTC hour

	// Months clearly inside BST (April–September)
	if (month > 3 && month < 10)
		return true;
	// Months clearly outside BST (November–February)
	if (month < 3 || month > 10)
		return false;

	// Transition month: March — BST begins at 01:00 UTC on the last Sunday
	if (month == 3)
	{
		int ls = lastSunday(month, year);
		if (dayOfMonth > ls)  return true;
		if (dayOfMonth < ls)  return false;
		return (hour >= 1);   // transition day: active from 01:00 UTC
	}

	// Transition month: October — BST ends at 01:00 UTC on the last Sunday
	// (clocks go back, so BST is active until 01:00 UTC)
	if (month == 10)
	{
		int ls = lastSunday(month, year);
		if (dayOfMonth < ls)  return true;
		if (dayOfMonth > ls)  return false;
		return (hour < 1);    // transition day: active only before 01:00 UTC
	}

	return false;
}

// Call once per hour (or at boot) to update the cached BST flag.
void refreshBSTCache(struct tm *timeinfo)
{
	int day  = timeinfo->tm_mday;
	int hour = timeinfo->tm_hour;
	if (day != _isBSTCachedDay || hour != _isBSTCachedHour)
	{
		_isBSTActive     = isBST(timeinfo);
		_isBSTCachedDay  = day;
		_isBSTCachedHour = hour;
		DEBUG_PRINTLN("BST cache updated: BST is" + String(_isBSTActive ? "" : " not") + " active");
	}
}

void checkBST()
{
	struct tm timeinfo;
#ifdef ESP32
	if (!getLocalTime(&timeinfo))
	{
		DEBUG_PRINTLN("Could not retrieve local time from ESP");
		return;
	}
	refreshBSTCache(&timeinfo);
	DEBUG_PRINTLN("BST is" + String(_isBSTActive ? "" : " not") + " applied");
#endif
}

void adjustBST(struct tm *timeinfo)
{
	// Use cached value — isBST() is NOT called every tick.
	if (_isBSTActive)
	{
		timeinfo->tm_hour += 1;
		if (timeinfo->tm_hour == 24)
			timeinfo->tm_hour = 0;
	}
}

void setupTimeClient()
{
	struct tm timeinfo;

#ifdef ESP32
	if (!getLocalTime(&timeinfo))
	{
		DEBUG_PRINTLN("Failed to obtain time");
	}
#endif
	bool __bst = isBST(&timeinfo);
	DEBUG_PRINTLN("Daylight Saving is " + String(__bst ? "" : "not ") + "applied");
	_timeClient.setTimeOffset(__bst ? 3600 : 0); // Set time offset based on BST
	_timeClient.begin();
	while (!_timeClient.forceUpdate())
	{
		DEBUG_PRINTLN(F("NTP delay"));
		delay(350);
	}
}

void setupMQTT(String deviceName, String mqttServerIP, uint16_t mqttServerPort)
{
	DEBUG_PRINT("MQTT client (" + deviceName + ") connecting to: ");
	DEBUG_PRINT(mqttServerIP.c_str());
	DEBUG_PRINT(":");
	DEBUG_PRINTLN(mqttServerPort);

	_mqttClient.setBufferSize(MQTT_MAX_PACKET_SIZE);
	IPAddress mqttAddr;
	if (mqttAddr.fromString(mqttServerIP)) {
		_mqttClient.setServer(mqttAddr, mqttServerPort);
	} else {
		// Fallback for hostnames: copy to a persistent buffer to avoid dangling pointer
		static char _mqttServerHostBuf[64];
		strncpy(_mqttServerHostBuf, mqttServerIP.c_str(), sizeof(_mqttServerHostBuf) - 1);
		_mqttServerHostBuf[sizeof(_mqttServerHostBuf) - 1] = '\0';
		_mqttClient.setServer(_mqttServerHostBuf, mqttServerPort);
	}
	delay(500);
	mqttReconnect(deviceName);
}

void setupMQTT(String mqttServerIP, uint16_t mqttServerPort)
{
	setupMQTT(_deviceClientName, mqttServerIP, mqttServerPort);
}

void setupMQTT()
{

	setupMQTT(_deviceClientName, MQTT_SERVER_IP, MQTT_SERVER_PORT);
}

void mqttSendInitStat()
{
	if (!_mqttClient.connected())
	{
		mqttReconnect();
	}
	if (_mqttClient.connected())
	{
		mqttTransmitInitStat();
	}
}

