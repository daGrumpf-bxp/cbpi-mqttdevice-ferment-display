// ============================================================================
// heartbeat.cpp
// ============================================================================
#include "heartbeat.h"
#include "state.h"

namespace heartbeat {

// Active-LOW LED: ON means writing LOW.
static inline void ledOn()  { digitalWrite(LED_BUILTIN, LOW);  }
static inline void ledOff() { digitalWrite(LED_BUILTIN, HIGH); }

// ---- Pulse overlay (event flash) -------------------------------------------
// When pulse() is called, the LED is forced OFF for PULSE_OFF_MS regardless
// of the background pattern. After that, the background pattern resumes.
static constexpr uint32_t PULSE_OFF_MS = 50;
static uint32_t s_pulse_end_ms = 0;

void pulse() {
    s_pulse_end_ms = millis() + PULSE_OFF_MS;
}

// ---- Background patterns ----------------------------------------------------
// Each pattern is described as a sequence of (on_ms, off_ms) phases.
// A "phase" is one segment of the blink cycle. Patterns loop indefinitely.

struct Phase {
    bool     on;          // LED state during this phase
    uint32_t duration_ms;
};

// READY: solid ON with a brief 100ms OFF dip every ~2s.
//   Looks like a slow "heartbeat" — confirms loop() is alive.
static const Phase PATTERN_READY[] = {
    { true,  1900 },
    { false,  100 },
};

// NO_MQTT: ON 200, OFF 100, ON 200, OFF 1500. Double-blink slow.
static const Phase PATTERN_NO_MQTT[] = {
    { true,   200 },
    { false,  100 },
    { true,   200 },
    { false, 1500 },
};

// NO_WIFI: fast symmetric 100/100.
static const Phase PATTERN_NO_WIFI[] = {
    { true,   100 },
    { false,  100 },
};

// BOOT: ON solid.
static const Phase PATTERN_BOOT[] = {
    { true,  10000 },
};

// ---- Pattern selection ------------------------------------------------------
struct Pattern {
    const Phase* phases;
    uint8_t      count;
};

static Pattern currentPattern() {
    using state::NetStatus;
    switch (state::g.net_status) {
        case NetStatus::SUBSCRIBED:
        case NetStatus::MQTT_OK:
            return { PATTERN_READY,
                     sizeof(PATTERN_READY)   / sizeof(Phase) };
        case NetStatus::WIFI_OK:
            return { PATTERN_NO_MQTT,
                     sizeof(PATTERN_NO_MQTT) / sizeof(Phase) };
        case NetStatus::DISCONNECTED:
        default:
            // BOOT pattern vs NO_WIFI: distinguish by whether we even
            // have a fermenter yet. Before first connect ever, BOOT.
            if (state::g.last_fermenter_ms == 0
                && millis() < 5000) {
                return { PATTERN_BOOT,
                         sizeof(PATTERN_BOOT) / sizeof(Phase) };
            }
            return { PATTERN_NO_WIFI,
                     sizeof(PATTERN_NO_WIFI) / sizeof(Phase) };
    }
}

// ---- Engine -----------------------------------------------------------------
static uint8_t  s_phase_index   = 0;
static uint32_t s_phase_start_ms = 0;

void begin() {
    pinMode(LED_BUILTIN, OUTPUT);
    ledOn();                  // visible ON at boot — "I'm powered"
    s_phase_start_ms = millis();
    s_phase_index    = 0;
}

void loop() {
    const uint32_t now = millis();

    // Pulse overlay takes priority: if we're in the middle of a pulse,
    // keep the LED OFF and don't advance the background pattern timer.
    if (now < s_pulse_end_ms) {
        ledOff();
        // Note: we deliberately do NOT advance s_phase_start_ms here.
        // The background pattern picks up exactly where it left off
        // when the pulse ends. This gives a clean "blip" effect.
        return;
    }

    Pattern p = currentPattern();
    if (p.count == 0) return;  // safety

    // Did we exceed the current phase's duration ?
    const Phase& cur = p.phases[s_phase_index % p.count];
    if ((now - s_phase_start_ms) >= cur.duration_ms) {
        s_phase_index    = (s_phase_index + 1) % p.count;
        s_phase_start_ms = now;
    }

    // Apply the (possibly new) phase's LED state.
    const Phase& active = p.phases[s_phase_index % p.count];
    if (active.on) ledOn(); else ledOff();
}

} // namespace heartbeat
