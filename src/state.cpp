// ============================================================================
// state.cpp
// ============================================================================
#include "state.h"
#include "config.h"

namespace state {

State g;

bool isStale() {
    if (!g.has_fermenter) return false;  // nothing to check yet
    const uint32_t now = millis();
    // Note: millis() overflows after ~49.7 days. Using unsigned subtraction
    // makes this overflow-safe.
    return (now - g.last_fermenter_ms) > DATA_STALE_MS
        || (now - g.last_sensor_ms)    > DATA_STALE_MS;
}

void dump() {
    Serial.printf("[state] fermenter=%s sensor=%s\n",
                  g.fermenter_name, g.sensor_id);
    Serial.printf("[state] cooler_id=%s cooler_topic=%s\n",
                  g.cooler_id, g.cooler_topic);
    if (g.heater_id[0] != '\0') {
        Serial.printf("[state] heater_id=%s heater_topic=%s\n",
                      g.heater_id, g.heater_topic);
    }
    Serial.printf("[state] T=%.2f target=%.2f mode=%s in_range=%d\n",
                  g.current_temp, g.target_temp,
                  g.mode == Mode::AUTO ? "AUTO" : "OFF",
                  g.in_range ? 1 : 0);
    Serial.printf("[state] cooler=%s heater=%s stale=%d display=%s\n",
                  g.cooler_on ? "ON" : "off",
                  g.heater_on ? "ON" : "off",
                  isStale() ? 1 : 0,
                  g.display_ready ? "ok" : "MISSING");
}

} // namespace state
