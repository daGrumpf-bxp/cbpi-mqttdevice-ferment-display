// ============================================================================
// cbpi_proto.cpp
// ============================================================================
// Reference payloads (from real CBPi4 dump, fermenter "Tornado"):
//
//   cbpi/fermenterupdate/HEYPZ4kMQDuQcWxLU26vFp
//   {
//     "id": "HEYPZ4kMQDuQcWxLU26vFp",
//     "name": "Tornado",
//     "state": false,                       <- false=OFF, true=AUTO
//     "sensor": "7eskHbgqTA9FNihrP6AJ59",
//     "cooler": "H2uFH5kPh8ttjBvKFhbgpb",
//     "heater": "",
//     "target_temp": 25,
//     "type": "Fermenter Hysteresis + AutoRestart",
//     ...
//   }
//
//   cbpi/sensordata/7eskHbgqTA9FNihrP6AJ59
//   {"id": "7eskHbgqTA9FNihrP6AJ59", "value": 15.875,
//    "datatype": "value", "inrange": true}
//
//   cbpi/actorupdate/H2uFH5kPh8ttjBvKFhbgpb     <- used to discover props.Topic
//   {"id": "H2uFH5kPh8ttjBvKFhbgpb", "name": "MQTT/Actor/4RB01/R01",
//    "type": "MQTTActor", "props": {"Topic": "actor/4RB01/R01"},
//    "state": true, "power": 100, ...}            <- state IGNORED (often stale)
//
//   actor/4RB01/R01                              <- REAL-TIME source of truth
//   {"state": "off", "power": 0}                  <- note: state is a STRING here
//
// Why we don't trust cbpi/actorupdate/* for actor state:
// - Its values are retained with retain=true and often stale.
// - CBPi only re-publishes on manual UI toggle, not on AUTO regulation.
// - The raw topic (actor/4RB01/R01), pushed by CBPi to drive the relay,
//   is updated in real-time on every state change.
// ============================================================================
#include "cbpi_proto.h"
#include "state.h"
#include "secrets.h"

#include <ArduinoJson.h>

namespace cbpi {

// Helper: safely copy a const char* into a fixed-size buffer.
static void copyStr(char* dst, size_t dst_size, const char* src) {
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

// Helper: extract the trailing segment of a topic after the last '/'.
// Returns pointer into the original string (no allocation).
static const char* topicTail(const char* topic) {
    const char* slash = strrchr(topic, '/');
    return slash ? (slash + 1) : topic;
}

// ---------------------------------------------------------------------------
bool parseFermenterUpdate(const char* payload, size_t len) {
    // Real Tornado payload is ~500 bytes. 1024 gives comfortable headroom.
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, len);
    if (err) {
        Serial.printf("[cbpi] fermenterupdate parse error: %s\n", err.c_str());
        return false;
    }

    // Defensive: confirm this is the fermenter we care about.
    const char* id = doc["id"] | "";
    if (strcmp(id, FERMENTER_ID) != 0) {
        Serial.printf("[cbpi] fermenterupdate ignored: id=%s != %s\n",
                      id, FERMENTER_ID);
        return false;
    }

    // Extract fields we use.
    const char* name    = doc["name"]   | "";
    const char* sensor  = doc["sensor"] | "";
    const char* cooler  = doc["cooler"] | "";
    const char* heater  = doc["heater"] | "";
    const bool  st      = doc["state"]  | false;
    const float target  = doc["target_temp"] | NAN;

    copyStr(state::g.fermenter_name, sizeof(state::g.fermenter_name), name);
    copyStr(state::g.sensor_id,      sizeof(state::g.sensor_id),      sensor);
    copyStr(state::g.cooler_id,      sizeof(state::g.cooler_id),      cooler);
    copyStr(state::g.heater_id,      sizeof(state::g.heater_id),      heater);
    state::g.target_temp       = target;
    state::g.mode              = st ? state::Mode::AUTO : state::Mode::OFF;
    state::g.has_fermenter     = true;
    state::g.last_fermenter_ms = millis();

    Serial.printf("[cbpi] fermenter=%s mode=%s target=%.2f sensor=%s cooler=%s\n",
                  name, st ? "AUTO" : "OFF", target, sensor, cooler);
    return true;
}

// ---------------------------------------------------------------------------
bool parseSensorData(const char* topic, const char* payload, size_t len) {
    // Only care about the sensor of our fermenter.
    if (state::g.sensor_id[0] == '\0') return false;

    const char* tail = topicTail(topic);
    if (strcmp(tail, state::g.sensor_id) != 0) {
        return false;  // some other sensor — silently ignore
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, len);
    if (err) {
        Serial.printf("[cbpi] sensordata parse error: %s\n", err.c_str());
        return false;
    }

    state::g.current_temp   = doc["value"]   | NAN;
    state::g.in_range       = doc["inrange"] | true;
    state::g.last_sensor_ms = millis();

    Serial.printf("[cbpi] sensor T=%.2f in_range=%d\n",
                  state::g.current_temp, state::g.in_range ? 1 : 0);
    return true;
}

// ---------------------------------------------------------------------------
bool parseActorUpdate(const char* topic, const char* payload, size_t len) {
    if (!state::g.has_fermenter) return false;

    const char* tail = topicTail(topic);

    // Is this our cooler or heater?
    const bool is_cooler = (state::g.cooler_id[0] != '\0')
                        && (strcmp(tail, state::g.cooler_id) == 0);
    const bool is_heater = (state::g.heater_id[0] != '\0')
                        && (strcmp(tail, state::g.heater_id) == 0);

    if (!is_cooler && !is_heater) return false;  // not ours

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, len);
    if (err) {
        Serial.printf("[cbpi] actorupdate parse error: %s\n", err.c_str());
        return false;
    }

    // We only care about props.Topic — the raw topic CBPi drives in real-time.
    // The "state" field of this payload is deliberately ignored (see header).
    const char* raw_topic = doc["props"]["Topic"] | "";
    if (raw_topic[0] == '\0') {
        Serial.println("[cbpi] actorupdate: no props.Topic, ignoring");
        return false;
    }

    if (is_cooler) {
        copyStr(state::g.cooler_topic, sizeof(state::g.cooler_topic), raw_topic);
        Serial.printf("[cbpi] cooler raw topic learned: %s\n", raw_topic);
    }
    if (is_heater) {
        copyStr(state::g.heater_topic, sizeof(state::g.heater_topic), raw_topic);
        Serial.printf("[cbpi] heater raw topic learned: %s\n", raw_topic);
    }
    return true;
}

// ---------------------------------------------------------------------------
bool parseActorRaw(const char* topic, const char* payload, size_t len) {
    if (!state::g.has_fermenter) return false;

    // Match against the raw topics we've learned (full topic match, no tail).
    const bool is_cooler = (state::g.cooler_topic[0] != '\0')
                        && (strcmp(topic, state::g.cooler_topic) == 0);
    const bool is_heater = (state::g.heater_topic[0] != '\0')
                        && (strcmp(topic, state::g.heater_topic) == 0);

    if (!is_cooler && !is_heater) return false;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, len);
    if (err) {
        Serial.printf("[cbpi] actorraw parse error: %s\n", err.c_str());
        return false;
    }

    // Raw topic payload uses a STRING for state ("on"/"off"), not a bool.
    const char* st = doc["state"] | "off";
    const bool  on = (strcmp(st, "on") == 0);

    if (is_cooler) state::g.cooler_on = on;
    if (is_heater) state::g.heater_on = on;
    state::g.last_actor_ms = millis();

    Serial.printf("[cbpi] %s raw=%s\n",
                  is_cooler ? "cooler" : "heater",
                  on ? "ON" : "off");
    return true;
}

} // namespace cbpi
