// ============================================================================
// config.h — compile-time constants and tunables
// ============================================================================
#pragma once

// ---- CBPi4 MQTT topic templates ---------------------------------------
// Patterns; final topics built at runtime by net_mqtt.cpp.
#define CBPI_TOPIC_FERMENTER_UPDATE  "cbpi/fermenterupdate/"  // + fermenter_id
#define CBPI_TOPIC_SENSOR_DATA       "cbpi/sensordata/"       // + sensor_id
#define CBPI_TOPIC_ACTOR_UPDATE      "cbpi/actorupdate/"      // + actor_id

// Topic we publish to at boot to force CBPi4 to re-emit fermenter state.
#define CBPI_TOPIC_REQUEST_UPDATE    "cbpi/updatefermenter"

// ---- MQTT tuning ------------------------------------------------------
// Short keepalive => faster detection of broker loss.
#define MQTT_KEEPALIVE_SECONDS   20
#define MQTT_RECONNECT_MS        5000   // delay between reconnect attempts

// ---- WiFi tuning ------------------------------------------------------
#define WIFI_RECONNECT_MS        5000
#define WIFI_BOOT_TIMEOUT_MS     30000  // log a warning if not connected after this

// ---- Display tuning ---------------------------------------------------
// I2C pins on Wemos D1 mini: SDA=D2 (GPIO4), SCL=D1 (GPIO5).
// These are the Arduino defaults for ESP8266; we keep them explicit anyway.
#define I2C_SDA_PIN              4   // D2
#define I2C_SCL_PIN              5   // D1
#define OLED_I2C_ADDR            0x3C
#define DISPLAY_REFRESH_MS       500  // redraw at most every 500ms

// ---- State staleness --------------------------------------------------
// If we haven't received an update for this long, consider the data stale
// and show a warning indicator on screen.
//
// CBPi4 pushes fermenterupdate at two cadences:
//   - Immediately (~1s) on any state change (mode toggle, setpoint, etc.)
//   - Periodically every 60s as a heartbeat ("MQTTUpdate" CBPi setting)
//
// So in steady state, the longest we should ever wait between updates is
// 60s. We set the stale threshold to 90s = 60s + 30s safety margin.
// Beyond that, something is genuinely wrong (CBPi down, broker down,
// network partition).
#define DATA_STALE_MS            90000  // 90s

// ---- Logging ----------------------------------------------------------
#define SERIAL_BAUD              115200
