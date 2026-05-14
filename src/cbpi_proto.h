// ============================================================================
// cbpi_proto.h — CBPi4 MQTT payload parsers
// ============================================================================
// Each parser reads the JSON payload and updates state::g accordingly.
// They return true on success, false on parse error (logged to serial).
//
// All parsers are tolerant of unknown fields (CBPi4 is a moving target),
// and validate the presence of required fields before mutating state.
// ============================================================================
#pragma once

#include <Arduino.h>

namespace cbpi {

// Parse a payload received on cbpi/fermenterupdate/<id>.
// Updates: fermenter_name, sensor_id, cooler_id, heater_id, target_temp, mode.
// Returns true if the payload matched our configured FERMENTER_ID.
bool parseFermenterUpdate(const char* payload, size_t len);

// Parse a payload received on cbpi/sensordata/<sensor_id>.
// Updates: current_temp, in_range — only if the sensor_id matches
// state::g.sensor_id (we ignore data for other sensors).
bool parseSensorData(const char* topic, const char* payload, size_t len);

// Parse a payload received on cbpi/actorupdate/<actor_id>.
// Used ONLY to discover the raw topic (props.Topic) for our cooler/heater,
// because cbpi/actorupdate/* itself has stale retained values and only
// updates on manual UI toggle (not on AUTO regulation).
// Updates: cooler_topic and/or heater_topic. The "state" field of this
// payload is intentionally IGNORED — see parseActorRaw() instead.
// Returns true if the actor_id matched our cooler or heater.
bool parseActorUpdate(const char* topic, const char* payload, size_t len);

// Parse a payload received on the raw actor topic (e.g. "actor/4RB01/R01").
// This is the real-time source of truth for actor state — what CBPi
// actually sends to the relay bridge in real-time.
// Payload format: {"state": "on"/"off", "power": int}
//                 (note: state is a string here, not a bool)
// Updates: cooler_on or heater_on, depending on which raw topic this is.
// Returns true if the topic matched our cooler_topic or heater_topic.
bool parseActorRaw(const char* topic, const char* payload, size_t len);

} // namespace cbpi
