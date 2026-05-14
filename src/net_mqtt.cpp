// ============================================================================
// net_mqtt.cpp — async MQTT, event-driven, with dynamic subscription tracking
// ============================================================================
#include "net_mqtt.h"
#include "net_wifi.h"
#include "secrets.h"
#include "config.h"
#include "state.h"
#include "cbpi_proto.h"
#include "heartbeat.h"

#include <AsyncMqttClient.h>

namespace net_mqtt {

// ---- module-private state --------------------------------------------------
static AsyncMqttClient s_client;
static uint32_t        s_last_reconnect_attempt = 0;
static bool            s_want_connected = false;

// ---- dynamic subscription tracking -----------------------------------------
// We track 5 subscriptions that depend on what we've learned from CBPi4:
//
//   1. s_sub_sensor       : cbpi/sensordata/<sensor_id>       (steady state)
//   2. s_sub_cooler_au    : cbpi/actorupdate/<cooler_id>      (EPHEMERAL)
//   3. s_sub_heater_au    : cbpi/actorupdate/<heater_id>      (EPHEMERAL)
//   4. s_sub_cooler_raw   : <cooler_topic>  e.g. actor/4RB01/R01 (steady)
//   5. s_sub_heater_raw   : <heater_topic>                       (steady)
//
// The actorupdate subscriptions are ephemeral: we subscribe to them only
// long enough to learn the raw topic (props.Topic), then unsubscribe to
// avoid stale retained data muddying the picture.
static char s_sub_sensor[64]     = "";
static char s_sub_cooler_au[64]  = "";
static char s_sub_heater_au[64]  = "";
static char s_sub_cooler_raw[64] = "";
static char s_sub_heater_raw[64] = "";

// Static topic for our fermenter (built once in begin()).
static char s_topic_fermenter[96] = "";

// ---- forward declarations --------------------------------------------------
static void onMqttConnect(bool sessionPresent);
static void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
static void onMqttSubscribe(uint16_t packetId, uint8_t qos);
static void onMqttMessage(char* topic, char* payload,
                          AsyncMqttClientMessageProperties props,
                          size_t len, size_t index, size_t total);

static void resyncDynamicSubscriptions();
static void connectIfNeeded();

// ---- helpers ---------------------------------------------------------------
static void buildTopic(char* dst, size_t dst_size,
                       const char* prefix, const char* suffix) {
    snprintf(dst, dst_size, "%s%s", prefix, suffix);
}

// Subscribe to `topic` if non-empty and not already subscribed.
// Updates *current to track what's now subscribed.
static void resubscribe(char* current, size_t current_size,
                        const char* prefix, const char* new_id) {
    char wanted[64];
    if (new_id && new_id[0] != '\0') {
        buildTopic(wanted, sizeof(wanted), prefix, new_id);
    } else {
        wanted[0] = '\0';
    }

    if (strcmp(current, wanted) == 0) return;  // no change

    if (current[0] != '\0') {
        Serial.printf("[mqtt] unsubscribe %s\n", current);
        s_client.unsubscribe(current);
    }
    if (wanted[0] != '\0') {
        Serial.printf("[mqtt] subscribe   %s\n", wanted);
        s_client.subscribe(wanted, 0);
    }
    strncpy(current, wanted, current_size - 1);
    current[current_size - 1] = '\0';
}

// Subscribe to a fixed topic string (no prefix building), unsub if id is empty.
static void resubscribeRaw(char* current, size_t current_size,
                           const char* wanted) {
    if (!wanted) wanted = "";
    if (strcmp(current, wanted) == 0) return;  // no change

    if (current[0] != '\0') {
        Serial.printf("[mqtt] unsubscribe %s\n", current);
        s_client.unsubscribe(current);
    }
    if (wanted[0] != '\0') {
        Serial.printf("[mqtt] subscribe   %s\n", wanted);
        s_client.subscribe(wanted, 0);
    }
    strncpy(current, wanted, current_size - 1);
    current[current_size - 1] = '\0';
}

// Called whenever something we learned from CBPi4 might have changed
// (cooler_id, heater_id, cooler_topic, heater_topic). Re-aligns all
// dynamic subscriptions accordingly.
//
// Two-stage subscription dance for actors:
//   - We sub to cbpi/actorupdate/<id> *only* if we don't yet know the raw
//     topic. As soon as cooler_topic/heater_topic is set, we unsub from
//     actorupdate to avoid noise/stale retained values.
static void resyncDynamicSubscriptions() {
    if (!s_client.connected()) return;

    // Sensor: always sub to cbpi/sensordata/<sensor_id>
    resubscribe(s_sub_sensor, sizeof(s_sub_sensor),
                CBPI_TOPIC_SENSOR_DATA, state::g.sensor_id);

    // Cooler: actorupdate only if we don't yet know the raw topic
    const char* cooler_au_id = (state::g.cooler_topic[0] == '\0')
                             ? state::g.cooler_id : "";
    resubscribe(s_sub_cooler_au, sizeof(s_sub_cooler_au),
                CBPI_TOPIC_ACTOR_UPDATE, cooler_au_id);
    resubscribeRaw(s_sub_cooler_raw, sizeof(s_sub_cooler_raw),
                   state::g.cooler_topic);

    // Heater: same pattern
    const char* heater_au_id = (state::g.heater_topic[0] == '\0')
                             ? state::g.heater_id : "";
    resubscribe(s_sub_heater_au, sizeof(s_sub_heater_au),
                CBPI_TOPIC_ACTOR_UPDATE, heater_au_id);
    resubscribeRaw(s_sub_heater_raw, sizeof(s_sub_heater_raw),
                   state::g.heater_topic);
}

// ---- callbacks -------------------------------------------------------------
static void onMqttConnect(bool sessionPresent) {
    Serial.printf("[mqtt] connected (session_present=%d)\n", sessionPresent);
    state::g.net_status = state::NetStatus::MQTT_OK;

    // Subscribe to the static fermenter topic.
    Serial.printf("[mqtt] subscribe   %s\n", s_topic_fermenter);
    s_client.subscribe(s_topic_fermenter, 0);

    // Request a fresh push of fermenter state so we don't wait for the
    // periodic update.  Publishing to cbpi/updatefermenter with any payload
    // triggers CBPi4 to re-emit cbpi/fermenterupdate/* immediately.
    Serial.println("[mqtt] requesting fermenter resync");
    s_client.publish(CBPI_TOPIC_REQUEST_UPDATE, 0, false, "{}");

    // Re-subscribe to any dynamic topics we already knew about (after a
    // reconnect, the broker forgot our subscriptions).
    s_sub_sensor[0]     = '\0';
    s_sub_cooler_au[0]  = '\0';
    s_sub_heater_au[0]  = '\0';
    s_sub_cooler_raw[0] = '\0';
    s_sub_heater_raw[0] = '\0';
    resyncDynamicSubscriptions();
}

static void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
    Serial.printf("[mqtt] DISCONNECTED reason=%d\n", (int)reason);
    state::g.net_status = net_wifi::isConnected()
                          ? state::NetStatus::WIFI_OK
                          : state::NetStatus::DISCONNECTED;
    // Forget current subscriptions; they'll be redone on reconnect.
    s_sub_sensor[0]     = '\0';
    s_sub_cooler_au[0]  = '\0';
    s_sub_heater_au[0]  = '\0';
    s_sub_cooler_raw[0] = '\0';
    s_sub_heater_raw[0] = '\0';
}

static void onMqttSubscribe(uint16_t packetId, uint8_t qos) {
    // We mark "fully subscribed" once we've heard back from the broker at
    // least once. This isn't strictly correct (multiple subscribes) but it
    // gives a useful UI signal.
    state::g.net_status = state::NetStatus::SUBSCRIBED;
}

static void onMqttMessage(char* topic, char* payload,
                          AsyncMqttClientMessageProperties /*props*/,
                          size_t len, size_t /*index*/, size_t /*total*/) {
    // Brief LED flash to signal "something arrived" — useful for visual
    // confirmation that traffic is flowing without watching serial.
    heartbeat::pulse();

    // Quick router by topic prefix.
    if (strncmp(topic, CBPI_TOPIC_FERMENTER_UPDATE,
                strlen(CBPI_TOPIC_FERMENTER_UPDATE)) == 0) {
        if (cbpi::parseFermenterUpdate(payload, len)) {
            // sensor/cooler/heater IDs may have changed — re-align subs.
            resyncDynamicSubscriptions();
        }
    } else if (strncmp(topic, CBPI_TOPIC_SENSOR_DATA,
                       strlen(CBPI_TOPIC_SENSOR_DATA)) == 0) {
        cbpi::parseSensorData(topic, payload, len);
    } else if (strncmp(topic, CBPI_TOPIC_ACTOR_UPDATE,
                       strlen(CBPI_TOPIC_ACTOR_UPDATE)) == 0) {
        if (cbpi::parseActorUpdate(topic, payload, len)) {
            // We just learned a props.Topic — sub to raw, unsub actorupdate.
            resyncDynamicSubscriptions();
        }
    } else {
        // Last resort: maybe a raw actor topic (e.g. actor/4RB01/R01).
        // parseActorRaw checks against state::g.cooler_topic/heater_topic.
        cbpi::parseActorRaw(topic, payload, len);
    }
}

// ---- public API ------------------------------------------------------------
void begin() {
    buildTopic(s_topic_fermenter, sizeof(s_topic_fermenter),
               CBPI_TOPIC_FERMENTER_UPDATE, FERMENTER_ID);

    Serial.printf("[mqtt] init broker=%s:%d device=%s\n",
                  MQTT_HOST, MQTT_PORT, DEVICE_NAME);

    s_client.onConnect(onMqttConnect);
    s_client.onDisconnect(onMqttDisconnect);
    s_client.onSubscribe(onMqttSubscribe);
    s_client.onMessage(onMqttMessage);

    s_client.setServer(MQTT_HOST, MQTT_PORT);
    s_client.setKeepAlive(MQTT_KEEPALIVE_SECONDS);
    s_client.setClientId(DEVICE_NAME);
    if (MQTT_USER[0] != '\0') {
        s_client.setCredentials(MQTT_USER, MQTT_PASS);
    }

    s_want_connected = true;
}

static void connectIfNeeded() {
    if (s_client.connected()) return;
    if (!net_wifi::isConnected()) return;  // wait for WiFi first

    const uint32_t now = millis();
    if ((now - s_last_reconnect_attempt) < MQTT_RECONNECT_MS) return;

    Serial.println("[mqtt] connecting...");
    s_client.connect();
    s_last_reconnect_attempt = now;
}

void loop() {
    if (!s_want_connected) return;
    connectIfNeeded();
}

bool isConnected() {
    return s_client.connected();
}

} // namespace net_mqtt
