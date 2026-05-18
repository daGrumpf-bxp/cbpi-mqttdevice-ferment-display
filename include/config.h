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

// I2C bus clock. 100kHz is the safe default and works with any cable length
// up to ~50cm. Raise to 400000 (400kHz) if you want faster refresh AND
// your wiring is short. Lower to 50000 (50kHz) if you see I2C glitches
// (e.g. on long dupont cables in a noisy electrical environment).
#define I2C_CLOCK_HZ             100000UL

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

// ---- Watchdog ---------------------------------------------------------
// "Let it crash" philosophy: this device is idempotent (CBPi4 is the
// source of truth), so when something goes seriously wrong we just
// reboot to a known-good state.
//
// We watch a single metric: time since the last fresh MQTT message.
// If we go 3 CBPi4 heartbeats without hearing anything (3 × 60s + 1s
// margin = 181s), something is broken upstream from us (WiFi down,
// MQTT broker down, CBPi4 crashed) and rebooting is the cheap option.
#define WDT_DATA_STALE_MS        (181UL * 1000UL)  // 3min 01s

// Grace period at boot: never reboot before MIN_UPTIME_MS, to give
// WiFi/MQTT a chance to connect and the first messages to arrive.
#define WDT_MIN_UPTIME_MS        (5UL * 60UL * 1000UL)  // 5 min

// Daily preventive reboot. Set to wall-clock time (NTP-synced), not
// uptime, so the reboot never lands during a brewing session.
// 04:00 local time = "definitely not brewing" everywhere.
#define WDT_DAILY_REBOOT_HOUR    4
#define WDT_DAILY_REBOOT_MIN     0

// ---- NTP / time zone --------------------------------------------------
// NTP is used by the watchdog's daily-reboot feature. If NTP never syncs
// (no internet, closed LAN, etc.), the firmware operates normally but the
// daily reboot is silently disabled — only the data-stale watchdog runs.
//
// NTP servers can be overridden in secrets.h to point at a local time
// source (typical for closed LAN deployments where the customer's router
// or a PDC runs NTP). If not overridden, falls back to public servers.
#ifndef NTP_SERVER_1
#define NTP_SERVER_1             "pool.ntp.org"
#endif
#ifndef NTP_SERVER_2
#define NTP_SERVER_2             "time.nist.gov"
#endif

// POSIX TZ string for France (CET/CEST, DST automatic).
// Override in secrets.h for other regions, e.g.:
//   #define NTP_TZ "UTC0"                             // pure UTC
//   #define NTP_TZ "EST5EDT,M3.2.0,M11.1.0"           // US East
#ifndef NTP_TZ
#define NTP_TZ                   "CET-1CEST,M3.5.0,M10.5.0/3"
#endif

// If NTP hasn't synced after this many seconds since WiFi UP, log a
// warning. This helps detect a misconfigured NTP server early.
#define NTP_SYNC_WARN_MS         (60UL * 1000UL)

// Set to 0 in secrets.h to disable the daily preventive reboot entirely
// (useful if you can't or won't expose NTP to the device).
#ifndef WDT_DAILY_REBOOT_ENABLED
#define WDT_DAILY_REBOOT_ENABLED 1
#endif

// ---- LWT (Last Will & Testament) --------------------------------------
// Topic where the broker publishes our online/offline status. Allows
// external observers (Home Assistant, Node-RED, dashboards) to know
// when this device is alive without polling.
//
// LWT_TOPIC is built at runtime as "display/<DEVICE_NAME>/status".
// Values published:
//   "online"     — set by us right after MQTT connect succeeds
//   "rebooting"  — set by us before a planned reboot (data-stale, daily)
//   "offline"    — set by the broker automatically when we drop the TCP
//                  connection without sending DISCONNECT first (= crash)
#define LWT_TOPIC_PREFIX         "display/"
#define LWT_TOPIC_SUFFIX         "/status"

// status field values (used as the "value" key in JSON payload)
#define LWT_VALUE_ONLINE         "online"
#define LWT_VALUE_REBOOTING      "rebooting"
#define LWT_VALUE_OFFLINE        "offline"

// The LWT payload is FIXED at the time we register it with the broker, so
// the ts can't be dynamic — the broker publishes it verbatim later when
// the client drops. We mark this explicitly in the payload so observers
// know to use their own reception timestamp for "offline" events.
#define LWT_PAYLOAD_OFFLINE_JSON \
    "{\"value\":\"offline\",\"ts\":\"unknown, set by broker LWT\"}"

#define LWT_LAST_REBOOT_SUFFIX   "/last_reboot_reason"
