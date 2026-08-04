#ifndef FLIGHT_TRACKER_H
#define FLIGHT_TRACKER_H

#include <ArduinoJson.h>
//#include <WiFiClient.h>
#include <HTTPClient.h>
#include <limits>
#include <math.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// Allocator that routes ArduinoJson memory to PSRAM instead of internal SRAM.
// The T-Display-AMOLED has 8MB of PSRAM; internal heap is only ~300KB.
struct SpiRamAllocator : ArduinoJson::Allocator {
    void* allocate(size_t size) {
        return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    void deallocate(void* pointer) {
        heap_caps_free(pointer);
    }
    void* reallocate(void* ptr, size_t new_size) {
        return heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
};
inline SpiRamAllocator _spiRamAllocator;
using SpiRamJsonDocument = JsonDocument;

inline const char* host = "192.168.1.48"; // Flight data source
inline const char* path = "/data/aircraft.json";
inline const int port = 8080;

inline const float myLat = 51.39513478804202;
inline const float myLon = -1.338836382480781;

constexpr bool VERBOSE_AIRCRAFT_JSON_DEBUG = false;

struct AircraftDetailsStruct {
    String callsign;
    String type;
    String category;
    int squawk;
    String route;
    int altitude;
    float distance;
    float speed;
    String status;
    String identifier;
    String flight;
    String description;
    float latitude;
    float longitude;
    float heading;
    float track;
    bool hasHeading;
    bool hasTrack;
    bool identifierUnknown; // Flag to indicate if identifier is unknown
};

struct FlightStats {
    int totalAircraft=0;
    int emergencyCount=0;
    float avgAltitude=0;
    float avgSpeed;
    byte highestAircraft;
    byte lowestAircraft;
    byte fastestAircraft;
    byte slowestAircraft;
    byte closestAircraft;
    byte farthestAircraft;
    int emergencyAircraft[10]; // Store up to 10 emergencies
    AircraftDetailsStruct aircraft[100]; // Store up to 100 aircraft
};

inline FlightStats _flightStats;

// --- FreeRTOS fetch-task globals ---
// PSRAM-backed JSON document, allocated once at boot. 64KB is negligible
// against the 8MB PSRAM and avoids per-cycle heap churn.
inline SpiRamJsonDocument _flightDetailsJSONDoc(&_spiRamAllocator);

// Staging struct — written exclusively by the fetch task on core 0.
// Swapped into _flightStats under mutex when a fetch completes.
inline FlightStats _flightStatsStaging;

// Mutex protecting _flightStats during swap (fetch task) and render reads (loop task).
inline SemaphoreHandle_t _flightStatsMutex = nullptr;

// Handle to the fetch task — used by the main loop to send task notifications.
inline TaskHandle_t _fetchTaskHandle = nullptr;

// Set true while the fetch task is running; prevents re-triggering.
inline volatile bool _fetchInProgress = false;
// --- end FreeRTOS fetch-task globals ---

#define EARTH_RADIUS_KM 6371.0
//#define DEG_TO_RAD 0.017453292519943295

inline float haversine(float lat1, float lon1, float lat2, float lon2) {

    if (VERBOSE_AIRCRAFT_JSON_DEBUG) {
        DEBUG_PRINTLN("Calculating haversine");
    }
    float dLat = radians(lat2 - lat1);
    float dLon = radians(lon2 - lon1);

    float a = sin(dLat / 2) * sin(dLat / 2) +
              cos(radians(lat1)) * cos(radians(lat2)) *
              sin(dLon / 2) * sin(dLon / 2);

    float c = 2 * atan2(sqrtf(a), sqrtf(1 - a));

    if (VERBOSE_AIRCRAFT_JSON_DEBUG) {
        DEBUG_PRINT("dLat: "); DEBUG_PRINTLNDEC(dLat, 6);
        DEBUG_PRINT("dLon: "); DEBUG_PRINTLNDEC(dLon, 6);
        DEBUG_PRINT("a: "); DEBUG_PRINTLNDEC(a, 6);
        DEBUG_PRINT("c: "); DEBUG_PRINTLNDEC(c, 6);
    }

    return EARTH_RADIUS_KM * c;
}

inline String getFlightStatus(float verticalRate) {
    if (verticalRate > 500) return "Ascending";
    if (verticalRate < -500) return "Descending";
    return "Cruising";
}

// Fetches aircraft.json and parses it directly into doc via streaming.
// Uses a filter to discard unused fields before they consume heap space.
// Returns true on success. Avoids holding the full raw JSON in a String.
inline bool fetchFlightData(const char* host, const char* path, const int port, SpiRamJsonDocument &doc) {
    WiFiClient client;

    DEBUG_PRINTLN("Connecting to " + String(host) + ":" + String(port));
    if (!client.connect(host, port)) {
        DEBUG_PRINTLN("Connection failed!");
        return false;
    }
    client.setTimeout(5000);

    // HTTP/1.0 avoids chunked transfer encoding, which would embed hex chunk-size
    // markers in the body and corrupt the JSON stream.
    DEBUG_PRINTLN("Sending GET request to " + String(host) + ":" + String(port) + String(path));
    client.print(String("GET ") + path + " HTTP/1.0\r\n" +
                 "Host: " + host + "\r\n" +
                 "Connection: close\r\n\r\n");

    DEBUG_PRINTLN("Fetching response");

    // Skip HTTP headers
    DEBUG_PRINTLN("Skipping Headers");
    while (client.connected() || client.available()) {
        String line = client.readStringUntil('\n');
        if (line == "\r" || line == "\r\n" || line.length() == 0) {
            break;
        }
    }

    // Filter: only keep the fields we actually use in processFlightData().
    // ArduinoJson discards everything else before allocating heap, cutting
    // document memory by ~70-80% compared to parsing the full JSON.
    JsonDocument filter;
    JsonObject filterAircraft = filter["aircraft"][0].to<JsonObject>();
    filterAircraft["callsign"]     = true;
    filterAircraft["type"]         = true;
    filterAircraft["category"]     = true;
    filterAircraft["squawk"]       = true;
    filterAircraft["route"]        = true;
    filterAircraft["alt_baro"]     = true;
    filterAircraft["gs"]           = true;
    filterAircraft["baro_rate"]    = true;
    filterAircraft["flight"]       = true;
    filterAircraft["r"]            = true;
    filterAircraft["desc"]         = true;
    filterAircraft["lat"]          = true;
    filterAircraft["lon"]          = true;
    filterAircraft["true_heading"] = true;
    filterAircraft["track"]        = true;

    // Stream-parse directly from the TCP socket — no intermediate String buffer.
    DEBUG_PRINTLN("Streaming and parsing JSON");
    DeserializationError error = deserializeJson(doc, client, DeserializationOption::Filter(filter));

    if (error) {
        DEBUG_PRINTLN("JSON parsing failed! " + String(error.c_str()));
        return false;
    }
    return true;
}

inline void printAircraft(AircraftDetailsStruct AircraftToPrint)
{
    if (!VERBOSE_AIRCRAFT_JSON_DEBUG) return;

    DEBUG_PRINTLN("-----------------------------------------------------------------");
    DEBUG_PRINTLN("Aircraft:     "+AircraftToPrint.identifier);
    DEBUG_PRINTLN("  Altitude:   "+String(AircraftToPrint.altitude));
    DEBUG_PRINTLN("  callsign:   "+AircraftToPrint.callsign);
    DEBUG_PRINTLN("  description:"+AircraftToPrint.description);
    DEBUG_PRINTLN("  distance:   "+String(AircraftToPrint.distance));
    DEBUG_PRINTLN("  flight:     "+AircraftToPrint.flight);
    DEBUG_PRINTLN("  latitude:   "+String(AircraftToPrint.latitude));
    DEBUG_PRINTLN("  longitude:  "+String(AircraftToPrint.longitude));
    DEBUG_PRINTLN("  heading:    "+String(AircraftToPrint.heading));
    DEBUG_PRINTLN("  route:      "+AircraftToPrint.route);
    DEBUG_PRINTLN("  speed:      "+String(AircraftToPrint.speed));
    DEBUG_PRINTLN("  squawk:     "+String(AircraftToPrint.squawk));
    DEBUG_PRINTLN("  status:     "+AircraftToPrint.status);
    DEBUG_PRINTLN("-----------------------------------------------------------------");
}


inline bool isSquawkEmergency(int squawkCode) {
    /*
    Squawk codes are assigned by air traffic control and can be changed as needed to manage air traffic. Some common squawk codes and their meanings include:
    Squawk 7000: This is the ‘conspicuity code’ for VFR aircraft that are not assigned a specific code by ATC.     
    Squawk 2000: This is the ‘conspicuity code’ for IFR aircraft that are not assigned a specific code by ATC.     
    Squawk 7700: This is the emergency squawk code, and indicates that the aircraft is in distress and needs priority handling from air traffic control.    
    Squawk 7500: This code indicates that the aircraft is subject to unlawful interference (hijack).    
    Squawk 7600: This code indicates that the aircraft has experienced a radio failure and is unable to transmit or receive messages.    
    Squawk 0030:: This code indicates that the aircraft is lost (UK specific). 
    */
    return (squawkCode == 0030 || squawkCode == 7600 || squawkCode == 7500 || squawkCode == 7700 || squawkCode == 2000);
}

inline void processFlightData(SpiRamJsonDocument &doc, FlightStats &target)
{

    DEBUG_PRINTLN("processFlightData()\n");

    JsonArray aircraft = doc["aircraft"].as<JsonArray>();
    target.totalAircraft = aircraft.size();

    DEBUG_PRINTLN("processFlightData:populating AircraftDetailsStructs");
    DEBUG_PRINTLN("Total Aircraft from JSON: " + String(target.totalAircraft));

    float __highestAircraftaltitude = 0; // Initialize to 0
    target.highestAircraft = 0;

    float __lowestAircraftaltitude = std::numeric_limits<int>::max(); // Initialize to max value
    target.lowestAircraft = 0;

    float __fastestAircraftspeed = 0; // Initialize to 0
    target.fastestAircraft = 0;

    float __slowestAircraftspeed = std::numeric_limits<float>::max(); // Initialize to max value
    target.slowestAircraft = 0;

    float __closestAircraftdistance = std::numeric_limits<float>::max(); // Initialize to max value
    target.closestAircraft = 0;

    float __farthestAircraftdistance = 0; // Initialize to 0
    target.farthestAircraft = 0;

    target.emergencyCount = 0;

    float totalAltitude = 0;
    float totalSpeed = 0;
    int __currentAircraftIndex = 0;
    for (JsonObject plane : aircraft) {

        if (__currentAircraftIndex >= (int)(sizeof(target.aircraft) / sizeof(target.aircraft[0]))) {
            DEBUG_PRINTLN("Aircraft limit reached; ignoring remaining entries");
            break;
        }

        AircraftDetailsStruct __currentAircraft = {
            plane["callsign"] | "Unknown",
            plane["type"] | "Unknown",
            plane["category"] | "",
            plane["squawk"].as<int>() | 0,
            plane["route"] | "Unknown",
            plane["alt_baro"] | 0,
            plane["distance"],
            plane["gs"], // gs=ground speed; tas = true air speed (ias=indicated air speed)
            getFlightStatus(plane["baro_rate"] | 0), //status
            plane["flight"] | "Unknown", //identifier
            plane["r"] | "Unknown", //flight
            plane["desc"] | "Unknown", //aircraft type
            plane["lat"].as<float>(),
            plane["lon"].as<float>(),
            plane["true_heading"].as<float>(),
            plane["track"].as<float>(),
            plane["true_heading"].is<float>(),
            plane["track"].is<float>(),
            false
        };

        __currentAircraft.callsign.trim();
        __currentAircraft.flight.trim();
        if (__currentAircraft.callsign.length() == 0 || __currentAircraft.callsign == "Unknown") {
            __currentAircraft.identifier = __currentAircraft.flight;
            __currentAircraft.identifierUnknown = __currentAircraft.identifier.length() == 0 ||
                __currentAircraft.identifier == "Unknown";
        } else {
            __currentAircraft.identifier = __currentAircraft.callsign;
        }

        float distance = haversine(myLat, myLon, __currentAircraft.latitude, __currentAircraft.longitude);
        __currentAircraft.distance = distance;


        printAircraft(__currentAircraft);

        if (__currentAircraft.altitude==0 || __currentAircraft.speed==0 )
        {
            DEBUG_PRINTLN("Skipping aircraft: "+__currentAircraft.identifier+" as is likely on the ground and/or not moving");
            continue;
        }
        __currentAircraft.description.trim();
        __currentAircraft.route.trim();
        __currentAircraft.identifier.trim();


        totalAltitude += __currentAircraft.altitude;
        totalSpeed += __currentAircraft.speed;

        target.aircraft[__currentAircraftIndex] = __currentAircraft;

        DEBUG_PRINTLN("Calculating highest, lowest, fastest, slowest, closest, farthest, emergency");
        //highest, lowest, fastest, slowest, closest, farthest, emergency:
        if (__currentAircraft.altitude > __highestAircraftaltitude) {
            target.highestAircraft = __currentAircraftIndex;
            __highestAircraftaltitude = __currentAircraft.altitude;
        }

        if (__currentAircraft.altitude < __lowestAircraftaltitude && __currentAircraft.altitude > 0 && __currentAircraft.speed > 0) {
            target.lowestAircraft = __currentAircraftIndex;
            __lowestAircraftaltitude = __currentAircraft.altitude;
        }
        if (__currentAircraft.speed > __fastestAircraftspeed) {
            target.fastestAircraft = __currentAircraftIndex;
            __fastestAircraftspeed = __currentAircraft.speed;
        }
        if (__currentAircraft.speed < __slowestAircraftspeed && __currentAircraft.speed > 0) {
            target.slowestAircraft = __currentAircraftIndex;
            __slowestAircraftspeed = __currentAircraft.speed;
        }
        if (distance > 0 && distance < __closestAircraftdistance ) {
            target.closestAircraft = __currentAircraftIndex;
            __closestAircraftdistance = distance;
        }
        if (distance > __farthestAircraftdistance) {
            target.farthestAircraft = __currentAircraftIndex;
            __farthestAircraftdistance = distance;
        }
        if (isSquawkEmergency(__currentAircraft.squawk) &&
            target.emergencyCount < (int)(sizeof(target.emergencyAircraft) / sizeof(target.emergencyAircraft[0]))) {
            target.emergencyAircraft[target.emergencyCount++] = __currentAircraftIndex;
        }


        __currentAircraftIndex++;

        target.totalAircraft = __currentAircraftIndex;
        DEBUG_PRINTLN("Aircraft details captured " + String(__currentAircraftIndex));
    }

    target.avgAltitude = (target.totalAircraft > 0) ? totalAltitude / target.totalAircraft : 0;
    target.avgSpeed = (target.totalAircraft > 0) ? totalSpeed / target.totalAircraft : 0;
}


inline void printFlightStats() {

        
    DEBUG_PRINTLN("Closest Aircraft Details:"); 
    printAircraft(_flightStats.aircraft[_flightStats.closestAircraft]);

    
    DEBUG_PRINTLN("Emergency Aircraft Details:  *****\n");
    for (int i = 0; i < _flightStats.emergencyCount; i++) {
        printAircraft(_flightStats.aircraft[_flightStats.emergencyAircraft[i]]);
    }
}

#endif // FLIGHT_TRACKER_H
