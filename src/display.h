// ============================================================================
// display.h — render fermenter state to the OLED
// ============================================================================
// Phase 1: SSD1306 128x64 I2C via U8g2.
// Phase 2: replace this file with a TFT_eSPI / LovyanGFX-based renderer for
// the ST7789V 240x320, keeping the same begin()/loop() API.
// ============================================================================
#pragma once

#include <Arduino.h>

namespace display {

void begin();
void loop();   // throttled internally to DISPLAY_REFRESH_MS

} // namespace display
