// ============================================================================
// heartbeat.h — onboard LED status indicator
// ============================================================================
// The D1 mini onboard blue LED is on GPIO2 (D4), active LOW.
//   digitalWrite(LED_BUILTIN, LOW)  -> LED ON
//   digitalWrite(LED_BUILTIN, HIGH) -> LED OFF
//
// This module drives the LED in a non-blocking way:
//   - It picks a steady "background pattern" based on net_status
//     (READY / NO_MQTT / NO_WIFI), so the LED state confirms at a glance
//     that the device is powered and how well it is connected.
//   - Any module can call pulse() to overlay a brief LED OFF flash on top
//     of the background pattern, to signal "an event just happened"
//     (e.g. an MQTT message was received).
//
// Conventions, visually:
//   READY:    ON solid, brief OFF flash every 2s  -> "I'm alive and online"
//   NO_MQTT:  double-blink slow                   -> "WiFi OK, MQTT down"
//   NO_WIFI:  fast symmetric blink                -> "WiFi down"
//   BOOT:     ON solid (no blinking)              -> "starting up"
// ============================================================================
#pragma once

#include <Arduino.h>

namespace heartbeat {

void begin();
void loop();

// Call this when something interesting happens (e.g. MQTT message arrived).
// Overlays a brief LED OFF flash on top of whatever pattern is running.
void pulse();

} // namespace heartbeat
