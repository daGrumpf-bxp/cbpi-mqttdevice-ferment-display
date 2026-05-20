// ============================================================================
// display.cpp — ST7789V 240x320 TFT in landscape (320x240) via TFT_eSPI
// ============================================================================
// Public API (begin/loop) is unchanged from the SSD1306 version. The internal
// rendering is completely rewritten because:
//   - TFT_eSPI uses direct-draw (no framebuffer) — required on ESP8266 which
//     can't hold a 153 KB RGB565 framebuffer in its 50 KB usable RAM.
//   - Color is now available: we use it sparingly for status accents only
//     (green AUTO, yellow MANUAL, red stale). Body of the layout stays
//     white-on-black for OLED-like readability at distance.
//   - Layout is landscape 320x240, scaled-up version of the SSD1306 layout
//     plus a thin footer with IP / local time / firmware version / uptime.
//
// Direct-draw means clearScreen() per redraw would cause flicker — we instead
// track what changed and only redraw the regions that actually need it.
// For Phase 1 (this branch) we redraw everything every DISPLAY_REFRESH_MS but
// only when at least one field actually changed since the last frame. This
// is cheap enough at 2 Hz and avoids over-engineering before we measure
// actual flicker on hardware.
// ============================================================================
#include "display.h"
#include "config.h"
#include "state.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <time.h>

namespace display {

// ---- TFT object and layout constants ---------------------------------------
static TFT_eSPI s_tft;

// Landscape orientation: 320 wide x 240 tall.
//   setRotation(1) = USB connector on the right
//   setRotation(3) = USB connector on the left
// Try the other value if your mounting is mirrored.
#ifndef TFT_ROTATION
#define TFT_ROTATION 1
#endif

static constexpr int16_t W = 320;
static constexpr int16_t H = 240;

// Vertical band offsets (top y of each region).
static constexpr int16_t Y_TITLE    = 0;
static constexpr int16_t Y_TEMP     = 32;
static constexpr int16_t Y_STATUS   = 134;
static constexpr int16_t Y_RESERVED = 184;
static constexpr int16_t Y_FOOTER   = 222;

// Colors (TFT_eSPI 16-bit RGB565 macros).
static constexpr uint16_t C_BG       = TFT_BLACK;
static constexpr uint16_t C_FG       = TFT_WHITE;
// "Dim" = light grey for secondary text. Originally 0x52AA but that's
// almost invisible on a real IPS panel — on a SSD1306 OLED it was binary
// pixels-on/off so anything-not-black read as white. On TFT we need a
// genuine mid-grey to keep the visual hierarchy.
static constexpr uint16_t C_DIM      = 0xBDF7;   // ~RGB(189, 189, 189), light grey
static constexpr uint16_t C_DIVIDER  = 0x2104;   // very dark grey
static constexpr uint16_t C_FOOTER   = 0x9CD3;   // slightly dimmer than C_DIM, for footer
static constexpr uint16_t C_AUTO     = TFT_GREEN;
static constexpr uint16_t C_MANUAL   = TFT_YELLOW;
static constexpr uint16_t C_OFF      = C_DIM;
static constexpr uint16_t C_COOLING  = TFT_CYAN;
static constexpr uint16_t C_HEATING  = TFT_ORANGE;
static constexpr uint16_t C_STALE    = TFT_RED;
static constexpr uint16_t C_OK       = TFT_GREEN;

// ---- Init state + retry ---------------------------------------------------
static bool     s_display_ready    = false;
static uint32_t s_last_retry_ms    = 0;
static uint32_t s_last_draw_ms     = 0;
static constexpr uint32_t DISPLAY_RETRY_MS = 5000;

// ---- Cached previous values for dirty checking ----------------------------
// We compare on every loop tick; if nothing changed, we don't repaint.
// This avoids flicker on direct-draw screens with no framebuffer.
struct Cache {
    char     fermenter_name[32] = "";
    float    current_temp = 0.0f;
    float    target_temp  = 0.0f;
    state::Mode mode      = state::Mode::OFF;
    bool     cooler_on    = false;
    bool     heater_on    = false;
    // We must track presence of cooler/heater too: when MQTT first
    // populates cooler_id (~3s after boot), drawStatusLine() starts
    // drawing "not cool" — but only if we mark the status band dirty.
    // Tracking the presence as a boolean here is enough.
    bool     cooler_present = false;
    bool     heater_present = false;
    bool     stale        = false;
    state::NetStatus net_status = state::NetStatus::DISCONNECTED;
    char     local_ip[16] = "";
    int      footer_minute = -1;   // re-render footer at most once per min
};
static Cache s_cache;
static bool  s_first_draw_pending = true;   // forces full redraw on tick 1

// ---- Init helpers ---------------------------------------------------------
static bool tryInit() {
    s_tft.init();
    s_tft.setRotation(TFT_ROTATION);
    s_tft.fillScreen(C_BG);

    // Log effective dimensions: tells us at-a-glance if rotation is what
    // we expected. Landscape should print 320x240; portrait would be
    // 240x320 (in which case adjust TFT_ROTATION).
    const int16_t w = s_tft.width();
    const int16_t h = s_tft.height();
    Serial.printf("[disp] ST7789V init OK, effective dims %dx%d (rotation=%d)\n",
                  w, h, TFT_ROTATION);
    if (w != W || h != H) {
        Serial.printf("[disp] WARNING: expected %dx%d (landscape), got %dx%d. "
                      "Try changing TFT_ROTATION (0,1,2,3) in display.cpp.\n",
                      W, H, w, h);
    }

    // Short text splash — visible enough to know we booted, gone fast
    // enough not to delay the first real frame.
    s_tft.setTextColor(C_FG, C_BG);
    s_tft.setTextDatum(MC_DATUM);
    s_tft.drawString("cbpi-mqttdevice", W/2, H/2 - 20, 4);
    s_tft.drawString("ferment-display", W/2, H/2 + 10, 4);
    s_tft.drawString("booting...", W/2, H/2 + 40, 2);
    delay(300);
    s_tft.fillScreen(C_BG);

    return true;
}

// ---- Drawing helpers ------------------------------------------------------
// Erase a horizontal band (used before redrawing a region).
static void clearBand(int16_t y, int16_t h) {
    s_tft.fillRect(0, y, W, h, C_BG);
}

static void drawDivider(int16_t y) {
    s_tft.drawFastHLine(8, y, W - 16, C_DIVIDER);
}

// Title bar: fermenter name (left), WIFI/MQTT indicators (right).
static void drawTitleBar() {
    clearBand(Y_TITLE, 30);
    s_tft.setTextDatum(TL_DATUM);  // top-left
    s_tft.setTextColor(C_FG, C_BG);
    s_tft.drawString(state::g.fermenter_name[0] ? state::g.fermenter_name
                                                : "(waiting...)",
                     8, Y_TITLE + 4, 4);

    // Connectivity indicators: WIFI then MQTT, right-aligned.
    using state::NetStatus;
    const NetStatus s = state::g.net_status;
    const bool wifi_ok = (s == NetStatus::WIFI_OK
                       || s == NetStatus::MQTT_OK
                       || s == NetStatus::SUBSCRIBED);
    const bool mqtt_ok = (s == NetStatus::MQTT_OK
                       || s == NetStatus::SUBSCRIBED);

    s_tft.setTextDatum(TR_DATUM);  // top-right
    s_tft.setTextColor(mqtt_ok ? C_OK : C_DIM, C_BG);
    s_tft.drawString("MQTT", W - 8, Y_TITLE + 8, 2);
    s_tft.setTextColor(wifi_ok ? C_OK : C_DIM, C_BG);
    s_tft.drawString("WIFI", W - 56, Y_TITLE + 8, 2);

    drawDivider(Y_TITLE + 30);
}

// Temperature block: huge current, "->" arrow, smaller target on the right.
// Stale "!" appended after target if data is too old.
static void drawTempBlock() {
    clearBand(Y_TEMP, 100);
    char buf[16];

    // Current temperature (large, font 7 = ~48px tall 7-segment numerals).
    if (isnan(state::g.current_temp)) {
        snprintf(buf, sizeof(buf), "--.-");
    } else {
        snprintf(buf, sizeof(buf), "%.1f", state::g.current_temp);
    }
    s_tft.setTextColor(C_FG, C_BG);
    s_tft.setTextDatum(ML_DATUM);
    s_tft.drawString(buf, 12, Y_TEMP + 50, 7);

    // °C unit, smaller, near the current temp.
    s_tft.setTextDatum(BL_DATUM);
    s_tft.drawString("`C", 152, Y_TEMP + 38, 4);   // `C reads as °C in font 4

    // Arrow pointing from current to target, drawn as primitives. Using
    // text "->" doesn't work in font 6 (digit-only) and looks ugly in
    // other fonts because the dash and ">" don't align nicely. A solid
    // arrow rendered with fillRect (shaft) + fillTriangle (head) gives
    // a clean, readable indicator that scales well visually.
    {
        const int16_t ay = Y_TEMP + 50;  // arrow centerline y
        // Shaft: 30px long, 5px thick.
        s_tft.fillRect(170, ay - 2, 30, 5, C_FG);
        // Head: pointed triangle, 14px wide.
        s_tft.fillTriangle(200, ay - 9,
                           200, ay + 9,
                           214, ay,
                           C_FG);
    }

    // Target temperature (smaller, font 6).
    if (isnan(state::g.target_temp)) {
        snprintf(buf, sizeof(buf), "--.-");
    } else {
        snprintf(buf, sizeof(buf), "%.1f", state::g.target_temp);
    }
    s_tft.setTextDatum(ML_DATUM);
    s_tft.drawString(buf, 222, Y_TEMP + 50, 6);

    // Stale "!" indicator after target if data is too old.
    if (state::isStale()) {
        s_tft.setTextColor(C_STALE, C_BG);
        s_tft.setTextDatum(MR_DATUM);
        s_tft.drawString("!", W - 8, Y_TEMP + 50, 6);
    }

    drawDivider(Y_TEMP + 100);
}

// Status line: cooler/heater verb on the left, mode (AUTO/MANUAL/OFF) right.
static void drawStatusLine() {
    clearBand(Y_STATUS, 50);
    s_tft.setTextDatum(ML_DATUM);

    // Cooler/heater label. Verbose case to avoid misreading at a glance.
    if (state::g.cooler_id[0] != '\0') {
        if (state::g.cooler_on) {
            s_tft.setTextColor(C_COOLING, C_BG);
            s_tft.drawString("COOLING *", 12, Y_STATUS + 25, 4);
        } else {
            s_tft.setTextColor(C_DIM, C_BG);
            s_tft.drawString("not cool", 12, Y_STATUS + 25, 4);
        }
    }
    if (state::g.heater_id[0] != '\0') {
        if (state::g.heater_on) {
            s_tft.setTextColor(C_HEATING, C_BG);
            s_tft.drawString("HEATING *", 140, Y_STATUS + 25, 4);
        } else {
            s_tft.setTextColor(C_DIM, C_BG);
            s_tft.drawString("not heat", 140, Y_STATUS + 25, 4);
        }
    }

    // Mode: AUTO (green) / MANUAL (yellow) / OFF (dim grey), right-aligned.
    const char* mode_str;
    uint16_t    mode_col;
    if (state::g.mode == state::Mode::AUTO) {
        mode_str = "AUTO";
        mode_col = C_AUTO;
    } else if (state::g.cooler_on || state::g.heater_on) {
        mode_str = "MANUAL";
        mode_col = C_MANUAL;
    } else {
        mode_str = "OFF";
        mode_col = C_OFF;
    }
    s_tft.setTextColor(mode_col, C_BG);
    s_tft.setTextDatum(MR_DATUM);
    s_tft.drawString(mode_str, W - 12, Y_STATUS + 25, 4);

    drawDivider(Y_STATUS + 50);
}

// Footer: IP / local time / firmware build / uptime — small dim text.
static void drawFooter() {
    clearBand(Y_FOOTER, 18);
    s_tft.setTextColor(C_FOOTER, C_BG);

    // IP, left-aligned.
    s_tft.setTextDatum(ML_DATUM);
    s_tft.drawString(state::g.local_ip[0] ? state::g.local_ip : "(no ip)",
                     6, Y_FOOTER + 9, 1);

    // Local time, centered. Falls back to uptime if NTP not synced.
    char tbuf[24];
    const time_t now_t = time(nullptr);
    if (now_t >= 1577836800UL /* 2020 */) {
        struct tm tm_local;
        localtime_r(&now_t, &tm_local);
        snprintf(tbuf, sizeof(tbuf), "%02d:%02d local",
                 tm_local.tm_hour, tm_local.tm_min);
    } else {
        snprintf(tbuf, sizeof(tbuf), "no NTP");
    }
    s_tft.setTextDatum(MC_DATUM);
    s_tft.drawString(tbuf, W/2, Y_FOOTER + 9, 1);

    // Uptime, right-aligned. Format adapts: "12m", "3h42m", "5d3h".
    const uint32_t up_s = millis() / 1000UL;
    char ubuf[16];
    if (up_s < 60UL * 60UL) {
        snprintf(ubuf, sizeof(ubuf), "up %lum", (unsigned long)(up_s / 60UL));
    } else if (up_s < 24UL * 3600UL) {
        snprintf(ubuf, sizeof(ubuf), "up %luh%02lum",
                 (unsigned long)(up_s / 3600UL),
                 (unsigned long)((up_s / 60UL) % 60UL));
    } else {
        snprintf(ubuf, sizeof(ubuf), "up %lud%luh",
                 (unsigned long)(up_s / 86400UL),
                 (unsigned long)((up_s / 3600UL) % 24UL));
    }
    s_tft.setTextDatum(MR_DATUM);
    s_tft.drawString(ubuf, W - 6, Y_FOOTER + 9, 1);
}

// Detect what changed between cache and current state. Returns a bitfield
// describing which regions need redraw. Bit 0 = title, 1 = temp, 2 = status,
// 3 = footer.
static uint8_t computeDirty() {
    uint8_t dirty = 0;
    if (strcmp(s_cache.fermenter_name, state::g.fermenter_name) != 0
        || s_cache.net_status != state::g.net_status) {
        dirty |= 0x01;
    }
    if (s_cache.current_temp != state::g.current_temp
        || s_cache.target_temp  != state::g.target_temp
        || s_cache.stale        != state::isStale()) {
        dirty |= 0x02;
    }
    if (s_cache.mode          != state::g.mode
        || s_cache.cooler_on     != state::g.cooler_on
        || s_cache.heater_on     != state::g.heater_on
        || s_cache.cooler_present != (state::g.cooler_id[0] != '\0')
        || s_cache.heater_present != (state::g.heater_id[0] != '\0')) {
        dirty |= 0x04;
    }
    // Footer changes when IP changes, or once per minute (for the clock),
    // or every redraw if uptime crossed a 1-minute boundary.
    const time_t now_t = time(nullptr);
    int cur_minute = -1;
    if (now_t >= 1577836800UL) {
        struct tm tm_local;
        localtime_r(&now_t, &tm_local);
        cur_minute = tm_local.tm_min;
    }
    if (strcmp(s_cache.local_ip, state::g.local_ip) != 0
        || s_cache.footer_minute != cur_minute) {
        dirty |= 0x08;
        s_cache.footer_minute = cur_minute;
    }
    return dirty;
}

static void commitCache() {
    strncpy(s_cache.fermenter_name, state::g.fermenter_name,
            sizeof(s_cache.fermenter_name) - 1);
    s_cache.current_temp   = state::g.current_temp;
    s_cache.target_temp    = state::g.target_temp;
    s_cache.mode           = state::g.mode;
    s_cache.cooler_on      = state::g.cooler_on;
    s_cache.heater_on      = state::g.heater_on;
    s_cache.cooler_present = (state::g.cooler_id[0] != '\0');
    s_cache.heater_present = (state::g.heater_id[0] != '\0');
    s_cache.stale          = state::isStale();
    s_cache.net_status     = state::g.net_status;
    strncpy(s_cache.local_ip, state::g.local_ip,
            sizeof(s_cache.local_ip) - 1);
}

// ---- public API -----------------------------------------------------------
void begin() {
    s_display_ready = tryInit();
    s_last_retry_ms = millis();
    state::g.display_ready = s_display_ready;
    s_first_draw_pending = true;   // force complete repaint on first loop()
}

void loop() {
    const uint32_t now = millis();

    if (!s_display_ready) {
        if ((now - s_last_retry_ms) < DISPLAY_RETRY_MS) return;
        s_last_retry_ms = now;
        s_display_ready = tryInit();
        state::g.display_ready = s_display_ready;
        if (s_display_ready) {
            Serial.println("[disp] TFT recovered after retry");
            s_first_draw_pending = true;   // repaint everything from scratch
        }
        if (!s_display_ready) return;
    }

    if ((now - s_last_draw_ms) < DISPLAY_REFRESH_MS) return;
    s_last_draw_ms = now;

    uint8_t dirty = computeDirty();
    if (s_first_draw_pending) {
        s_first_draw_pending = false;
        dirty = 0x0F;   // all 4 regions
    }
    if (dirty == 0) return;

    if (dirty & 0x01) drawTitleBar();
    if (dirty & 0x02) drawTempBlock();
    if (dirty & 0x04) drawStatusLine();
    if (dirty & 0x08) drawFooter();

    commitCache();
}

} // namespace display
