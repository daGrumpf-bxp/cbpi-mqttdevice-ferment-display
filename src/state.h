// ============================================================================
// state.h — shared application state (single source of truth)
// ============================================================================
// Holds everything we've learned from CBPi4 about the fermenter we're
// displaying. All MQTT callbacks write here; the display layer reads here.
//
// Single-core ESP8266 + AsyncMqttClient runs callbacks in main loop context,
// so no locking is needed. On ESP32 we'd need a mutex.
// ============================================================================
#pragma once

#include <Arduino.h>

namespace state {

// ---- Fermenter regulation mode ----
// CBPi4 fermenters are binary (state=true => AUTO, state=false => OFF).
// There is no "manual" mode for fermenters (unlike kettles).
enum class Mode : uint8_t {
    OFF  = 0,
    AUTO = 1
};

// ---- Connectivity status ----
enum class NetStatus : uint8_t {
    DISCONNECTED = 0,
    WIFI_OK      = 1,
    MQTT_OK      = 2,
    SUBSCRIBED   = 3
};

struct State {
    // -- Connectivity --
    NetStatus net_status = NetStatus::DISCONNECTED;

    // Local IP address as dotted string, e.g. "10.23.79.7". Populated by
    // net_wifi on GotIP. Shown on the display footer for quick reference
    // (so we can ping/SSH/HTTP-flash without consulting the router).
    char local_ip[16] = "";

    // -- Hardware presence --
    // True once the display has been successfully initialised. The
    // firmware will retry init periodically if this is false (so the
    // display can be hot-plugged at runtime without rebooting the device).
    bool display_ready = false;

    // -- Fermenter identity (received from cbpi/fermenterupdate/<id>) --
    char fermenter_name[32]  = "";   // e.g. "Tornado"
    char sensor_id[32]       = "";   // e.g. "7eskHbgqTA9FNihrP6AJ59"
    char cooler_id[32]       = "";   // e.g. "H2uFH5kPh8ttjBvKFhbgpb"
    char heater_id[32]       = "";   // may be empty
    bool has_fermenter       = false;  // set true after first fermenterupdate

    // -- Raw actor topics (learned via cbpi/actorupdate/<id> -> props.Topic) --
    // These are the topics CBPi pushes to in real-time to command the relay
    // bridge (e.g. "actor/4RB01/R01"). They are the single source of truth
    // for actor state — cbpi/actorupdate/* itself only updates on manual
    // toggle (and uses retain=true with sometimes-stale values).
    char cooler_topic[48]    = "";
    char heater_topic[48]    = "";

    // -- Live data --
    float current_temp       = NAN;
    float target_temp        = NAN;
    Mode  mode               = Mode::OFF;
    bool  in_range           = true;   // CBPi4 sensor "inrange" flag
    bool  cooler_on          = false;  // actor state
    bool  heater_on          = false;  // actor state

    // -- Freshness tracking (millis() of last update) --
    uint32_t last_fermenter_ms = 0;
    uint32_t last_sensor_ms    = 0;
    uint32_t last_actor_ms     = 0;
};

// Global state instance — defined in state.cpp.
extern State g;

// Returns true if any tracked data is older than DATA_STALE_MS.
bool isStale();

// Log full state to serial (for debugging).
void dump();

} // namespace state
