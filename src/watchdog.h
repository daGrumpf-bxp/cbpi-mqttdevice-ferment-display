// ============================================================================
// watchdog.h — applicative watchdog ("let it crash" school)
// ============================================================================
// Two reboot triggers:
//   1. DATA STALE: no fresh MQTT data for WDT_DATA_STALE_MS (181s by default,
//      = 3 × CBPi4 heartbeats + 1s margin). Indicates something upstream is
//      broken (WiFi down, broker down, CBPi crashed) — reboot to recover.
//   2. DAILY: every day at WDT_DAILY_REBOOT_HOUR (04:00 by default) to clear
//      any slow memory leak, lwIP state weirdness, etc.
//
// Before any planned reboot we publish "rebooting" + reason on the LWT topic
// so observers can distinguish a clean restart from a crash. The LWT itself
// will fire ("offline") only if the device truly crashes / loses power.
//
// Grace period: never reboot before WDT_MIN_UPTIME_MS so a flapping boot
// loop is impossible during initial WiFi/MQTT connection.
// ============================================================================
#pragma once

#include <Arduino.h>

namespace watchdog {

// Reasons for a planned reboot, published on the LWT "last_reboot_reason"
// retained topic so we can post-mortem after the device comes back.
enum class RebootReason : uint8_t {
    DATA_STALE = 0,
    DAILY      = 1,
};

void begin();
void loop();

// Called by net_mqtt when a connection becomes established. Publishes any
// reboot reason that was persisted to RTC before a previous restart.
// Safe to call multiple times — no-op after the first successful flush.
void flushPendingMqtt();

// Called by net_mqtt right before a deliberate ESP.restart(), so we can
// publish "rebooting" cleanly on MQTT.
void publishRebootIntent(RebootReason reason);

} // namespace watchdog
