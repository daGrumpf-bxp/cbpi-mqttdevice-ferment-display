// ============================================================================
// net_mqtt.h — non-blocking, event-driven MQTT (AsyncMqttClient)
// ============================================================================
// AsyncMqttClient never blocks: connect, publish, subscribe all return
// immediately and notify us via callbacks. No while(!client.connected()).
//
// Subscription strategy:
//   1. Always subscribe to cbpi/fermenterupdate/<FERMENTER_ID> (static).
//   2. When we receive a fermenterupdate, learn the sensor/cooler/heater
//      IDs and subscribe dynamically to those topics.
//   3. If those IDs change (sensor reassigned in CBPi4), unsubscribe the
//      old ones and subscribe the new ones.
// ============================================================================
#pragma once

#include <Arduino.h>

namespace net_mqtt {

void begin();
void loop();
bool isConnected();

// Publish a status string ("online", "rebooting") on display/<name>/status.
// Retained so observers see the current state on connect.
void publishStatus(const char* status, bool retain);

// Publish a reboot-reason on display/<name>/last_reboot_reason. Retained.
// Used by the watchdog right before a planned restart.
void publishLastRebootReason(const char* reason_id);

// Cleanly disconnect from the broker before a deliberate reboot, so the
// broker does NOT fire our LWT "offline" message (we already published
// "rebooting" via publishStatus()).
void shutdownClean();

} // namespace net_mqtt
