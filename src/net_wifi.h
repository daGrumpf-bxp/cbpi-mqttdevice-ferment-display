// ============================================================================
// net_wifi.h — non-blocking, event-driven WiFi management
// ============================================================================
// Uses WiFi.onStationModeXxx callbacks so we never block on a connect.
// loop() just polls reconnect state every WIFI_RECONNECT_MS.
// ============================================================================
#pragma once

#include <Arduino.h>

namespace net_wifi {

// Register WiFi event handlers and kick off the first connect attempt.
void begin();

// Call every loop() iteration. Non-blocking; just checks reconnect timer.
void loop();

// True if currently associated with an AP.
bool isConnected();

} // namespace net_wifi
