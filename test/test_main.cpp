// Host-side test of cbpi_proto parsers using real CBPi4 payloads.
#include "Arduino.h"
#include "state.h"
#include "cbpi_proto.h"
#include <cassert>
#include <cmath>

// Real payloads from Pierre's mosquitto_sub dump
const char* PAYLOAD_FERM_OFF = R"({
  "id": "HEYPZ4kMQDuQcWxLU26vFp",
  "name": "Tornado",
  "state": false,
  "sensor": "7eskHbgqTA9FNihrP6AJ59",
  "pressure_sensor": "",
  "heater": "",
  "cooler": "H2uFH5kPh8ttjBvKFhbgpb",
  "valve": "",
  "brewname": "",
  "description": null,
  "props": {
    "AutoResumeStateAfterReboot": "Yes",
    "AutoStart": "Yes",
    "CoolerOffsetOff": "0",
    "CoolerOffsetOn": "0.175",
    "HeaterOffsetOff": "",
    "HeaterOffsetOn": ""
  },
  "target_temp": 25,
  "target_pressure": 0,
  "type": "Fermenter Hysteresis + AutoRestart"
})";

const char* PAYLOAD_FERM_AUTO = R"({
  "id": "HEYPZ4kMQDuQcWxLU26vFp",
  "name": "Tornado",
  "state": true,
  "sensor": "7eskHbgqTA9FNihrP6AJ59",
  "pressure_sensor": "",
  "heater": "",
  "cooler": "H2uFH5kPh8ttjBvKFhbgpb",
  "target_temp": 25,
  "type": "Fermenter Hysteresis + AutoRestart"
})";

const char* PAYLOAD_FERM_WRONG_ID = R"({"id": "OTHER_FERM_ID", "name": "Foo"})";

const char* PAYLOAD_SENSOR = R"({"id": "7eskHbgqTA9FNihrP6AJ59", "value": 15.875, "datatype": "value", "inrange": true})";

const char* PAYLOAD_SENSOR_OTHER = R"({"id": "948gna8tzb9JL3Jtdkakcs", "value": -3.5, "datatype": "value", "inrange": true})";

const char* PAYLOAD_ACTOR_COOLER_ON = R"({"id": "H2uFH5kPh8ttjBvKFhbgpb", "name": "MQTT/Actor/4RB01/R01", "type": "MQTTActor", "props": {"Topic": "actor/4RB01/R01"}, "state": true, "power": 100, "output": 100, "maxoutput": 100, "timer": 0})";

const char* PAYLOAD_ACTOR_OTHER = R"({"id": "c42yQKAESSZ68tr5iLQVis", "state": false, "power": 0})";

int passed = 0, failed = 0;
#define CHECK(cond, msg) do { \
    if (cond) { passed++; printf("  PASS  %s\n", msg); } \
    else      { failed++; printf("  FAIL  %s\n", msg); } \
} while(0)

void test_fermenter_off() {
    printf("\n[test] fermenterupdate OFF\n");
    state::g = state::State{};  // reset
    bool ok = cbpi::parseFermenterUpdate(PAYLOAD_FERM_OFF, strlen(PAYLOAD_FERM_OFF));
    CHECK(ok, "parse returned true");
    CHECK(state::g.has_fermenter, "has_fermenter set");
    CHECK(strcmp(state::g.fermenter_name, "Tornado") == 0, "name == Tornado");
    CHECK(strcmp(state::g.sensor_id, "7eskHbgqTA9FNihrP6AJ59") == 0, "sensor_id captured");
    CHECK(strcmp(state::g.cooler_id, "H2uFH5kPh8ttjBvKFhbgpb") == 0, "cooler_id captured");
    CHECK(state::g.heater_id[0] == '\0', "heater_id empty");
    CHECK(state::g.target_temp == 25.0f, "target_temp == 25.0");
    CHECK(state::g.mode == state::Mode::OFF, "mode == OFF");
}

void test_fermenter_auto() {
    printf("\n[test] fermenterupdate AUTO\n");
    bool ok = cbpi::parseFermenterUpdate(PAYLOAD_FERM_AUTO, strlen(PAYLOAD_FERM_AUTO));
    CHECK(ok, "parse returned true");
    CHECK(state::g.mode == state::Mode::AUTO, "mode == AUTO");
}

void test_fermenter_wrong_id() {
    printf("\n[test] fermenterupdate ignored (wrong id)\n");
    char saved_name[32]; strcpy(saved_name, state::g.fermenter_name);
    bool ok = cbpi::parseFermenterUpdate(PAYLOAD_FERM_WRONG_ID, strlen(PAYLOAD_FERM_WRONG_ID));
    CHECK(!ok, "parse returned false");
    CHECK(strcmp(state::g.fermenter_name, saved_name) == 0, "state unchanged");
}

void test_sensor_match() {
    printf("\n[test] sensordata for our sensor\n");
    bool ok = cbpi::parseSensorData("cbpi/sensordata/7eskHbgqTA9FNihrP6AJ59",
                                    PAYLOAD_SENSOR, strlen(PAYLOAD_SENSOR));
    CHECK(ok, "parse returned true");
    CHECK(fabsf(state::g.current_temp - 15.875f) < 0.001f, "current_temp == 15.875");
    CHECK(state::g.in_range, "in_range true");
}

void test_sensor_other() {
    printf("\n[test] sensordata for someone else's sensor\n");
    float saved = state::g.current_temp;
    bool ok = cbpi::parseSensorData("cbpi/sensordata/948gna8tzb9JL3Jtdkakcs",
                                    PAYLOAD_SENSOR_OTHER, strlen(PAYLOAD_SENSOR_OTHER));
    CHECK(!ok, "parse returned false (ignored)");
    CHECK(state::g.current_temp == saved, "current_temp unchanged");
}

void test_actor_cooler() {
    printf("\n[test] actorupdate for our cooler (discovers raw topic)\n");
    bool ok = cbpi::parseActorUpdate("cbpi/actorupdate/H2uFH5kPh8ttjBvKFhbgpb",
                                     PAYLOAD_ACTOR_COOLER_ON, strlen(PAYLOAD_ACTOR_COOLER_ON));
    CHECK(ok, "parse returned true");
    CHECK(strcmp(state::g.cooler_topic, "actor/4RB01/R01") == 0,
          "cooler_topic discovered (actor/4RB01/R01)");
    // Verify the bogus 'state: true' from actorupdate was IGNORED:
    CHECK(!state::g.cooler_on, "cooler_on NOT updated from actorupdate");
}

void test_actor_other() {
    printf("\n[test] actorupdate for someone else's actor\n");
    char saved[48]; strcpy(saved, state::g.cooler_topic);
    bool ok = cbpi::parseActorUpdate("cbpi/actorupdate/c42yQKAESSZ68tr5iLQVis",
                                     PAYLOAD_ACTOR_OTHER, strlen(PAYLOAD_ACTOR_OTHER));
    CHECK(!ok, "parse returned false (ignored)");
    CHECK(strcmp(state::g.cooler_topic, saved) == 0,
          "cooler_topic unchanged");
}

// ---- new tests for the raw actor topic parser ----------------------------
const char* PAYLOAD_ACTOR_RAW_ON  = R"({"state": "on",  "power": 100})";
const char* PAYLOAD_ACTOR_RAW_OFF = R"({"state": "off", "power": 0})";

void test_actor_raw_on() {
    printf("\n[test] raw actor topic ON\n");
    // pre-cond: cooler_topic must already be set from previous test
    CHECK(strcmp(state::g.cooler_topic, "actor/4RB01/R01") == 0,
          "pre: cooler_topic is set");
    bool ok = cbpi::parseActorRaw("actor/4RB01/R01",
                                  PAYLOAD_ACTOR_RAW_ON, strlen(PAYLOAD_ACTOR_RAW_ON));
    CHECK(ok, "parse returned true");
    CHECK(state::g.cooler_on, "cooler_on == true (from raw)");
}

void test_actor_raw_off() {
    printf("\n[test] raw actor topic OFF\n");
    bool ok = cbpi::parseActorRaw("actor/4RB01/R01",
                                  PAYLOAD_ACTOR_RAW_OFF, strlen(PAYLOAD_ACTOR_RAW_OFF));
    CHECK(ok, "parse returned true");
    CHECK(!state::g.cooler_on, "cooler_on == false (from raw)");
}

void test_actor_raw_other() {
    printf("\n[test] raw actor topic for a different actor (ignored)\n");
    bool saved = state::g.cooler_on;
    bool ok = cbpi::parseActorRaw("actor/4RB01/R03",
                                  PAYLOAD_ACTOR_RAW_ON, strlen(PAYLOAD_ACTOR_RAW_ON));
    CHECK(!ok, "parse returned false (ignored)");
    CHECK(state::g.cooler_on == saved, "cooler_on unchanged");
}

int main() {
    printf("=== cbpi_proto host tests ===\n");
    test_fermenter_off();
    test_fermenter_auto();
    test_fermenter_wrong_id();
    test_sensor_match();
    test_sensor_other();
    test_actor_cooler();
    test_actor_other();
    test_actor_raw_on();
    test_actor_raw_off();
    test_actor_raw_other();
    printf("\n=== summary: %d passed, %d failed ===\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
