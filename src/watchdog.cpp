// ============================================================================
// watchdog.cpp
// ============================================================================
#include "watchdog.h"
#include "config.h"
#include "state.h"
#include "net_mqtt.h"

#include <Arduino.h>
#include <time.h>

namespace watchdog {

// ---- module state ----------------------------------------------------------
static uint32_t s_boot_ms             = 0;
static int      s_last_daily_check_day = -1;  // -1 = never checked yet

// Convert RebootReason to a stable string for MQTT publishing and logs.
static const char* reasonStr(RebootReason r) {
    switch (r) {
        case RebootReason::DATA_STALE: return "data_stale";
        case RebootReason::DAILY:      return "daily";
    }
    return "unknown";
}

// Map ESP.getResetReason() text to a stable lowercase id we can publish.
// Useful for post-mortem of the *previous* boot.
static const char* lastResetReasonId() {
    const String r = ESP.getResetReason();
    if (r.indexOf("Power")      >= 0) return "power_on";
    if (r.indexOf("Hardware")   >= 0) return "hardware_wdt";
    if (r.indexOf("Software")   >= 0) return "software_wdt";  // our reboot
    if (r.indexOf("Exception")  >= 0) return "exception";
    if (r.indexOf("WDT")        >= 0) return "wdt";
    if (r.indexOf("Deep-Sleep") >= 0) return "deep_sleep";
    if (r.indexOf("External")   >= 0) return "external_reset";
    return "unknown";
}

// Return the most recent millis() timestamp among the data sources we care
// about (sensor data + fermenter update). This is "the last time anything
// interesting happened on the MQTT side".
static uint32_t lastFreshMs() {
    uint32_t t = state::g.last_sensor_ms;
    if (state::g.last_fermenter_ms > t) t = state::g.last_fermenter_ms;
    // Note: we deliberately don't include last_actor_ms — actor events are
    // sparse (only when CBPi actually toggles a relay), so a long gap there
    // is normal. Sensor data, by contrast, comes every ~1s in steady state.
    return t;
}

// ---- public API ------------------------------------------------------------
void begin() {
    s_boot_ms = millis();
    Serial.printf("[wdt] init, last_reset=%s (\"%s\")\n",
                  lastResetReasonId(), ESP.getResetReason().c_str());
}

void publishRebootIntent(RebootReason reason) {
    Serial.printf("[wdt] publishing reboot intent: %s\n", reasonStr(reason));
    net_mqtt::publishStatus(LWT_PAYLOAD_REBOOTING, /*retain=*/true);
    net_mqtt::publishLastRebootReason(reasonStr(reason));
}

void loop() {
    const uint32_t now    = millis();
    const uint32_t uptime = now - s_boot_ms;

    // 1. Grace period: never reboot before MIN_UPTIME_MS.
    if (uptime < WDT_MIN_UPTIME_MS) return;

    // 2. Data-stale check.
    //    Only meaningful once we've actually started receiving data (so we
    //    don't reboot a brand-new device that's still waiting for its first
    //    fermenterupdate during connect).
    const uint32_t last_fresh = lastFreshMs();
    if (last_fresh > 0) {
        if ((now - last_fresh) > WDT_DATA_STALE_MS) {
            Serial.printf("[wdt] DATA STALE — no MQTT data for %lu ms, "
                          "rebooting\n", (unsigned long)(now - last_fresh));
            publishRebootIntent(RebootReason::DATA_STALE);
            net_mqtt::shutdownClean();
            delay(200);   // give TCP a moment to flush DISCONNECT
            ESP.restart();
        }
    }

    // 3. Daily preventive reboot — only if NTP has actually given us a
    //    valid local time. Before the year 2000, time isn't synced yet.
    time_t  now_t = time(nullptr);
    if (now_t < 1577836800UL /* 2020-01-01 */) return;

    struct tm tm_local;
    localtime_r(&now_t, &tm_local);

    // Run the check at most once per minute, on the configured wall-clock
    // moment. Use day-of-year to detect "we already fired today".
    if (tm_local.tm_hour == WDT_DAILY_REBOOT_HOUR
        && tm_local.tm_min == WDT_DAILY_REBOOT_MIN
        && tm_local.tm_yday != s_last_daily_check_day
        && uptime > (60UL * 60UL * 1000UL) /* uptime > 1h */) {

        s_last_daily_check_day = tm_local.tm_yday;
        Serial.printf("[wdt] DAILY reboot at %02d:%02d (uptime %lu ms)\n",
                      tm_local.tm_hour, tm_local.tm_min,
                      (unsigned long)uptime);
        publishRebootIntent(RebootReason::DAILY);
        net_mqtt::shutdownClean();
        delay(200);
        ESP.restart();
    }
}

} // namespace watchdog
