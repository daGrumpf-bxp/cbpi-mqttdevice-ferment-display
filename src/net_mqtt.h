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

} // namespace net_mqtt
