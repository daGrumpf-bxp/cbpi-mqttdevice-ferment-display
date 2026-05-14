// ============================================================================
// main.cpp — mqttdevice-ferment entry point
// ============================================================================
// Wire up: serial -> display -> wifi -> mqtt.
// loop() is dead simple: each module manages its own state machine.
//
// No delay(), no while(), no blocking I/O. Every module returns immediately
// from its loop() function, even when it has work to do later (timers).
// ============================================================================
#include <Arduino.h>

#include "config.h"
#include "secrets.h"
#include "state.h"
#include "net_wifi.h"
#include "net_mqtt.h"
#include "display.h"
#include "heartbeat.h"

// ---- periodic state dump ---------------------------------------------------
static uint32_t s_last_dump_ms = 0;
static void periodicStateDump() {
    const uint32_t now = millis();
    if ((now - s_last_dump_ms) < 10000) return;  // every 10s
    s_last_dump_ms = now;
    state::dump();
}

// ----------------------------------------------------------------------------
void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(50);  // tiny pause so the first banner lines aren't garbled
    Serial.println();
    Serial.println(F("============================================"));
    Serial.printf( "  mqttdevice-ferment  device=%s\n", DEVICE_NAME);
    Serial.printf( "  fermenter_id=%s\n", FERMENTER_ID);
    Serial.printf( "  build %s %s\n", __DATE__, __TIME__);
    Serial.println(F("============================================"));

    heartbeat::begin();     // LED ON ASAP -> visual "powered" confirmation
    display::begin();
    net_wifi::begin();
    net_mqtt::begin();
}

// ----------------------------------------------------------------------------
void loop() {
    net_wifi::loop();
    net_mqtt::loop();
    display::loop();
    heartbeat::loop();
    periodicStateDump();

    // Yield to the ESP8266 background tasks (WiFi stack, lwIP).
    // Not strictly required since nothing here blocks, but cheap insurance.
    yield();
}
