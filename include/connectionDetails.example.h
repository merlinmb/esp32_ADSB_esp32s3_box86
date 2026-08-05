#pragma once

#define WIFI_ACCESSPOINT "network1"
#define WIFI_ACCESSPOINT_PASSWORD "password1"

#define WIFI_ACCESSPOINT1 "network2"
#define WIFI_ACCESSPOINT_PASSWORD1 "password2"

int MQTT_MAX_PACKET_SIZE = 256; // Replace with your actual maximum MQTT packet size
const char* MQTT_SERVERADDRESS = "192.168.1.55";// Replace with your actual MQTT server address
const char* MQTT_CLIENTNAME = "espADSBMonitor"; // Replace with your actual MQTT client name
const char* ARDUINO_OTA_URI_SUFFIX = "/firmware"; // Replace with your actual OTA URI suffix    
const char* ARDUINO_OTA_UPDATE_USERNAME = "admin"; // Replace with your actual OTA update username
const char* ARDUINO_OTA_UPDATE_PASSWORD = "testpassword"; // Replace with your actual OTA update password

const char* GEOAPIFY_KEY = "GEOAPIFY_KEY_HERE"; // Replace with your actual Geoapify API key