// ============================================================================
// display.cpp — SSD1306 128x64 layout with U8g2
// ============================================================================
// Layout target (128x64):
//
//   +------------------------------+
//   | Tornado              W M     |   line 1 (10px): name + status icons
//   |------------------------------|
//   |                              |
//   |   15.9°C    >   25.0°C       |   middle: big current T° / target T°
//   |                              |
//   |------------------------------|
//   |  COOL  *           AUTO      |   bottom line: cooler state + mode
//   +------------------------------+
//
// We use full-buffer mode (firstPage/nextPage NOT needed) for simplicity.
// Frame ~1KB of RAM, fine on D1 mini.
// ============================================================================
#include "display.h"
#include "state.h"
#include "config.h"

#include <U8g2lib.h>
#include <Wire.h>

namespace display {

// SSD1306 128x64, hardware I2C, full framebuffer.
// Constructor: rotation=R0, reset=NONE (no reset pin wired).
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C s_u8g2(
    U8G2_R0, /*reset=*/U8X8_PIN_NONE);

static uint32_t s_last_draw_ms = 0;

// ---- helpers ----------------------------------------------------------------
static void drawNetIcons() {
    // Top-right corner: W (wifi) and M (mqtt) presence indicators.
    // Visible if connected, hidden otherwise.
    s_u8g2.setFont(u8g2_font_5x7_tf);
    using state::NetStatus;
    NetStatus s = state::g.net_status;

    const bool wifi_ok = (s == NetStatus::WIFI_OK
                       || s == NetStatus::MQTT_OK
                       || s == NetStatus::SUBSCRIBED);
    const bool mqtt_ok = (s == NetStatus::MQTT_OK
                       || s == NetStatus::SUBSCRIBED);

    if (wifi_ok) s_u8g2.drawStr(110, 8, "W");
    if (mqtt_ok) s_u8g2.drawStr(120, 8, "M");
}

static void drawTitleBar() {
    s_u8g2.setFont(u8g2_font_6x10_tf);
    const char* name = state::g.has_fermenter
                     ? state::g.fermenter_name
                     : "(waiting...)";
    s_u8g2.drawStr(0, 8, name);
    drawNetIcons();
    s_u8g2.drawHLine(0, 11, 128);
}

static void drawTempBlock() {
    char buf[16];

    // -- Current temperature (large) --
    s_u8g2.setFont(u8g2_font_logisoso24_tn);  // 24px tall numerals
    if (isnan(state::g.current_temp)) {
        snprintf(buf, sizeof(buf), "--.--");
    } else {
        snprintf(buf, sizeof(buf), "%.1f", state::g.current_temp);
    }
    // Approximate horizontal centering of left half.
    s_u8g2.drawStr(0, 42, buf);

    // -- Unit and arrow separator --
    s_u8g2.setFont(u8g2_font_6x10_tf);
    s_u8g2.drawStr(60, 28, "\xb0""C");   // "°C"

    s_u8g2.setFont(u8g2_font_open_iconic_arrow_2x_t);
    s_u8g2.drawGlyph(60, 45, 0x004f);    // right arrow

    // -- Target temperature (smaller) --
    s_u8g2.setFont(u8g2_font_logisoso16_tn);
    if (isnan(state::g.target_temp)) {
        snprintf(buf, sizeof(buf), "--.-");
    } else {
        snprintf(buf, sizeof(buf), "%.1f", state::g.target_temp);
    }
    s_u8g2.drawStr(80, 42, buf);

    // Stale warning: flash a '!' if data is too old.
    if (state::isStale()) {
        s_u8g2.setFont(u8g2_font_6x10_tf);
        s_u8g2.drawStr(122, 28, "!");
    }
}

static void drawStatusLine() {
    s_u8g2.drawHLine(0, 49, 128);
    s_u8g2.setFont(u8g2_font_6x10_tf);

    // Cooler indicator.
    if (state::g.cooler_id[0] != '\0') {
        if (state::g.cooler_on) {
            s_u8g2.drawStr(0, 62, "COOL *");   // active: with bullet
        } else {
            s_u8g2.drawStr(0, 62, "cool");     // idle: lowercase, no bullet
        }
    }
    // Heater indicator (only if a heater is configured).
    if (state::g.heater_id[0] != '\0') {
        const char* h = state::g.heater_on ? "HEAT *" : "heat";
        s_u8g2.drawStr(48, 62, h);
    }

    // Mode: AUTO or OFF, right-aligned.
    const char* mode = (state::g.mode == state::Mode::AUTO) ? "AUTO" : "OFF";
    const int w = s_u8g2.getStrWidth(mode);
    s_u8g2.drawStr(128 - w, 62, mode);
}

static void drawBootScreen() {
    s_u8g2.setFont(u8g2_font_6x10_tf);
    s_u8g2.drawStr(0,  10, "mqttdevice-ferment");
    s_u8g2.drawStr(0,  22, "boot...");
    using state::NetStatus;
    const char* nets = "?";
    switch (state::g.net_status) {
        case NetStatus::DISCONNECTED: nets = "wifi connecting"; break;
        case NetStatus::WIFI_OK:      nets = "mqtt connecting"; break;
        case NetStatus::MQTT_OK:      nets = "subscribing";     break;
        case NetStatus::SUBSCRIBED:   nets = "ready";           break;
    }
    s_u8g2.drawStr(0,  34, nets);
}

// ---- public API ------------------------------------------------------------
void begin() {
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    s_u8g2.setI2CAddress(OLED_I2C_ADDR << 1);  // U8g2 expects shifted addr
    s_u8g2.begin();
    s_u8g2.setFontMode(0);
    s_u8g2.setDrawColor(1);
    Serial.println("[disp] SSD1306 init OK");
}

void loop() {
    const uint32_t now = millis();
    if ((now - s_last_draw_ms) < DISPLAY_REFRESH_MS) return;
    s_last_draw_ms = now;

    s_u8g2.clearBuffer();
    if (!state::g.has_fermenter) {
        drawBootScreen();
    } else {
        drawTitleBar();
        drawTempBlock();
        drawStatusLine();
    }
    s_u8g2.sendBuffer();
}

} // namespace display
