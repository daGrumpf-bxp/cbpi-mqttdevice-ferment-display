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

// ---- RTC memory persistence for reboot reason ------------------------------
// Problem: the data-stale watchdog fires precisely when MQTT is unreachable
// (otherwise messages would keep arriving and reset the stale timer). So we
// can't publish "rebooting" or last_reboot_reason at the moment of reboot —
// the broker is unreachable.
//
// Solution: stash the reason in ESP8266 RTC memory (512 bytes that survive
// software reset as long as power is maintained). At the next boot, after
// MQTT reconnects, we read this and publish the reason retained.
//
// Layout: 4 bytes magic + 4 bytes enum + 4 bytes inverted-magic checksum
// = 12 bytes total, well under the 512-byte limit. Triple-redundant magic
// helps distinguish "we wrote this" from "RTC contained garbage from boot".
//
// RTC layout is byte-addressed but read/write in 4-byte words.
struct RtcRebootInfo {
    uint32_t magic;        // = 0xCAFEBABE if written by us
    uint32_t reason;       // RebootReason cast to uint32
    uint32_t magic_inv;    // = ~magic, integrity check
};
static_assert(sizeof(RtcRebootInfo) == 12, "RtcRebootInfo must be 12 bytes");

static constexpr uint32_t RTC_MAGIC      = 0xCAFEBABEUL;
static constexpr uint32_t RTC_OFFSET_BYTES = 64;  // skip first 64 bytes for
                                                  // safety (other libs may
                                                  // use the low addresses)

static bool s_pending_publish = false;   // true at boot if RTC had a reason
static RebootReason s_pending_reason = RebootReason::DATA_STALE;

// Store reason in RTC and clear it.
static void rtcStoreReason(RebootReason r) {
    RtcRebootInfo info;
    info.magic     = RTC_MAGIC;
    info.reason    = static_cast<uint32_t>(r);
    info.magic_inv = ~RTC_MAGIC;
    ESP.rtcUserMemoryWrite(RTC_OFFSET_BYTES / 4,
                           reinterpret_cast<uint32_t*>(&info),
                           sizeof(info));
}

// Try to read a previously-stored reason. Returns true if a valid record
// existed, and *out_reason is set. Always clears the slot after reading
// so subsequent boots won't see stale data.
static bool rtcReadAndClearReason(RebootReason* out_reason) {
    RtcRebootInfo info;
    if (!ESP.rtcUserMemoryRead(RTC_OFFSET_BYTES / 4,
                               reinterpret_cast<uint32_t*>(&info),
                               sizeof(info))) {
        return false;
    }
    const bool valid = (info.magic == RTC_MAGIC)
                    && (info.magic_inv == ~RTC_MAGIC)
                    && (info.reason <= static_cast<uint32_t>(RebootReason::DAILY));
    if (valid) {
        *out_reason = static_cast<RebootReason>(info.reason);
    }
    // Always clear (write zeros) so we don't keep seeing the same reason
    // on subsequent boots.
    info.magic = info.magic_inv = info.reason = 0;
    ESP.rtcUserMemoryWrite(RTC_OFFSET_BYTES / 4,
                           reinterpret_cast<uint32_t*>(&info),
                           sizeof(info));
    return valid;
}

// ---- module state ----------------------------------------------------------
static uint32_t s_boot_ms             = 0;
static int      s_last_daily_check_day = -1;  // -1 = never checked yet

// One-shot warning if NTP hasn't synced within NTP_SYNC_WARN_MS of boot.
// Only fires once per boot. Doesn't affect operation, just logs.
static bool     s_ntp_warned          = false;

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

    // Did we leave a reboot reason in RTC memory before the last restart?
    // If so, we'll publish it once MQTT is connected — see flushPending().
    RebootReason r;
    if (rtcReadAndClearReason(&r)) {
        s_pending_publish = true;
        s_pending_reason  = r;
        Serial.printf("[wdt] previous reboot reason recovered from RTC: %s\n",
                      reasonStr(r));
    }
}

void publishRebootIntent(RebootReason reason) {
    Serial.printf("[wdt] saving reboot intent to RTC: %s\n", reasonStr(reason));

    // ALWAYS persist to RTC first — works even when MQTT is unreachable
    // (which is the common case for DATA_STALE reboots).
    rtcStoreReason(reason);

    // ALSO try to publish over MQTT right now. If we're still connected
    // (typical for DAILY reboot), the broker sees "rebooting" immediately
    // and a Home Assistant observer can distinguish planned vs crash.
    // If MQTT is down (typical for DATA_STALE), these calls silently skip
    // — the RTC-persisted reason will be published at the next boot.
    net_mqtt::publishStatus(LWT_VALUE_REBOOTING, /*retain=*/true);
    net_mqtt::publishLastRebootReason(reasonStr(reason));
}

// Called by the MQTT module when a connection succeeds, to flush any
// pending reboot reason recovered from RTC at boot.
void flushPendingMqtt() {
    if (!s_pending_publish) return;
    s_pending_publish = false;
    Serial.printf("[wdt] publishing recovered reboot reason: %s\n",
                  reasonStr(s_pending_reason));
    net_mqtt::publishLastRebootReason(reasonStr(s_pending_reason));
}

void loop() {
    const uint32_t now    = millis();
    const uint32_t uptime = now - s_boot_ms;

    // One-shot NTP warning: if we've been up long enough but NTP still
    // hasn't given us a valid clock, log it once. This makes "closed LAN"
    // deployments visible in the serial output instead of silently
    // disabling the daily-reboot feature.
    if (!s_ntp_warned && uptime > NTP_SYNC_WARN_MS) {
        s_ntp_warned = true;
        time_t now_t = time(nullptr);
        if (now_t < 1577836800UL /* 2020-01-01 */) {
            Serial.printf("[wdt] WARNING: NTP not synced after %lu ms uptime — "
                          "daily reboot %s. Check NTP server reachability or "
                          "set NTP_SERVER_1 to a local time source.\n",
                          (unsigned long)uptime,
#if WDT_DAILY_REBOOT_ENABLED
                          "disabled until sync"
#else
                          "explicitly disabled in secrets.h"
#endif
                          );
        } else {
            // Print both UTC and local time, so misconfigured TZ shows up
            // visibly in the serial log. If they differ by the expected
            // offset (e.g. +1h or +2h for France), TZ is correctly applied.
            struct tm tm_utc, tm_local;
            gmtime_r(&now_t, &tm_utc);
            localtime_r(&now_t, &tm_local);
            Serial.printf("[wdt] NTP synced: UTC=%02d:%02d local=%02d:%02d (TZ=%s)\n",
                          tm_utc.tm_hour, tm_utc.tm_min,
                          tm_local.tm_hour, tm_local.tm_min,
                          NTP_TZ);
        }
    }

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

    // 3. Daily preventive reboot — only if enabled, and only if NTP has
    //    actually given us a valid local time. Before the year 2000, time
    //    isn't synced yet, so we silently skip.
#if WDT_DAILY_REBOOT_ENABLED
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
#endif
}

} // namespace watchdog
