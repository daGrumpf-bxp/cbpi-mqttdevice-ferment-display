# cbpi-mqttdevice-ferment-display

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
![Status: SPI port](https://img.shields.io/badge/status-SPI%20port%20%E2%80%93%20hardware%20validated-brightgreen)
![Target: ESP8266](https://img.shields.io/badge/target-Wemos%20D1%20mini-green)
![Server: CBPi4](https://img.shields.io/badge/server-CraftBeerPi4-orange)

Wall-mounted display next to a fermenter, driven by [CraftBeerPi4](https://github.com/PiBrewing/craftbeerpi4) over MQTT. Shows current and target temperature, regulation mode, and live cooler/heater state.

**Phase 1 (current)** — Wemos D1 mini (ESP8266) + ST7789V TFT 2.0" 240×320 SPI in landscape.
**Phase 2 (planned)** — port to ESP32 WROOM-32, same screen, plus rotary encoder and buttons for local setpoint and mode control.

> **Branch `feat/spi-display`**: this iteration replaces the previous SSD1306 0.96" I²C OLED with the larger ST7789V TFT in preparation for the ESP32 phase. The SSD1306 historical version remains accessible at git tag `v0.7.10` on `main`.

---

## What it shows

> 📷 Real ST7789V photos will land here after the first successful hardware bring-up. For the historical SSD1306 OLED photos that documented Phase 1's first iteration, see git tag `v0.7.10` on `main`.

Four states the display surfaces:

| State | Meaning |
|---|---|
| **OFF** | Fermenter not regulating. Cooler relay off. Nothing's happening — explicitly. |
| **AUTO idle** | CBPi regulates. Current T° is at or below target, so the cooler stays off. The system is "armed but quiet". |
| **AUTO cooling** | CBPi regulates. Current T° is above target — cooler is energized, hysteresis loop active. |
| **MANUAL** | Fermenter regulation is OFF, but a relay is energized anyway — meaning someone forced it on via the CBPi UI actor controls, bypassing the fermenter logic. Surfaced explicitly so operators don't assume "OFF means nothing's running". |

### Layout breakdown

```
0                                                  320
+----------------------------------------------------+ 0
|  Tornado                              WIFI  MQTT   |  title bar (32px)
+----------------------------------------------------+ 32
|                                                    |
|      11.6   °C      ->      20.0                   |  temperature (100px)
|                                                    |
+----------------------------------------------------+ 132
|                                                    |
|    not cool                          AUTO          |  status (52px)
|                                                    |
+----------------------------------------------------+ 184
|                                                    |
|     (reserved for Phase 2 UI extensions)           |  free space (38px)
|                                                    |
+----------------------------------------------------+ 222
|  10.23.79.7   14:32 local   v0.8.0    up 12m       |  footer (18px, dim)
+----------------------------------------------------+ 240
```

**Bottom-right status field** is computed live from CBPi4 state:
- **`AUTO`** in green — `fermenter.state == true` (CBPi regulating per hysteresis)
- **`MANUAL`** in yellow — `fermenter.state == false` but cooler or heater is energized (someone overrode)
- **`OFF`** in dim grey — everything idle

**Cooler/heater label** uses verbose case + color accent to avoid misreading at a glance:
- `not cool` (grey) / `COOLING *` (cyan)
- `not heat` (grey) / `HEATING *` (orange)

**Stale indicator**: a `!` in red appears at the right of the temperature band if no fresh data arrived in 90s. The applicative watchdog will reboot the device automatically if the data outage persists past 181 seconds.

**Footer** carries operator-useful info:
- Local IP — convenient for SSH/ping/HTTP-flash without consulting the router
- Local time (or "no NTP" when not synced yet) — at-a-glance check that NTP works
- Firmware version + uptime — quick health check

The onboard blue LED gives status at a glance, **without needing to look at the screen or serial**:

| LED pattern | Meaning |
|---|---|
| Solid ON with brief dip every 2s | **READY** — WiFi + MQTT + subscribed, everything healthy |
| Double-blink slow | WiFi OK, MQTT down (check broker, credentials) |
| Fast symmetric blink | WiFi down (check SSID, password, signal) |
| Solid ON, no variation | Boot in progress, or `loop()` is stuck (bug) |
| Brief OFF flash overlay | An MQTT message just arrived (visual confirmation of live traffic) |

---

## Quick start

### Prerequisites

- A Linux machine for development (Windows/macOS work too, paths and udev rules differ)
- A Wemos D1 mini board (clones with CH340 or CP2102 USB-serial work)
- A ST7789V TFT 2.0" 240×320 SPI display
- A running CBPi4 instance with MQTT enabled, a fermenter configured, and a working MQTT broker (typically `mosquitto` on the same Pi as CBPi4)

### Install PlatformIO (one-time setup)

PlatformIO is a CLI-friendly build system for embedded targets. No IDE required.

```bash
pip install --user -U platformio
sudo usermod -a -G dialout $USER       # logout/login after this
```

### Build and flash

```bash
git clone https://github.com/daGrumpf-bxp/cbpi-mqttdevice-ferment-display.git
cd cbpi-mqttdevice-ferment-display

cp include/secrets.h.example include/secrets.h
vi include/secrets.h                   # fill in WiFi, MQTT broker, FERMENTER_ID

pio run -t upload                      # builds, then uploads to /dev/ttyUSB0 (auto-detected)
pio device monitor                     # 115200 bauds — Ctrl-T then Ctrl-X to exit
```

Find your `FERMENTER_ID` by sniffing the broker:

```bash
mosquitto_sub -h <broker> -v -t 'cbpi/fermenterupdate/#'
# Toggle a fermenter in CBPi4 UI — the topic contains the ID.
```

### Wiring (D1 mini + ST7789V TFT 2.0" 240×320 SPI)

8 wires this time (vs 4 for the SSD1306 I²C). The hardware-SPI clock+MOSI pins are fixed on the D1 mini; the three control pins (CS, DC, RST) are GPIOs chosen to avoid the ESP8266 boot-strap pins.

| ST7789V | D1 mini | GPIO | Role |
|---|---|---|---|
| GND | GND | — | Ground |
| VCC | 3V3 | — | 3.3V (panel tolerates 5V on some breakouts, 3.3V is safer) |
| SCL / SCK | D5 | GPIO14 | HSPI clock (hardware-fixed) |
| SDA / MOSI | D7 | GPIO13 | HSPI data (hardware-fixed) |
| CS | D6 | GPIO12 | Chip select (moved off the boot-strap pin GPIO15) |
| DC | D1 | GPIO5 | Data/command select (any free GPIO) |
| RES | D2 | GPIO4 | Hardware reset (any free GPIO) |
| BLK | 3V3 | — | Backlight, always on |

**Why CS is NOT on the hardware SPI CS pin (GPIO15 / D8)**: GPIO15 is a boot-strap pin on the ESP8266 — it must be LOW at boot, otherwise the chip won't enter UART download mode (= you can't flash it). TFT modules typically pull CS high through their internal logic, which would brick the flashing workflow whenever the TFT is connected. We route CS to GPIO12 (D6) instead, which is the HSPI MISO line we don't use (TFT_MISO=-1 since we never read back from the screen). The trade-off: CS is now driven in software rather than by the HSPI hardware peripheral. This costs a few microseconds per transaction, irrelevant for a 2 Hz refresh.

**Pins to never use for TFT control on the ESP8266**:
- GPIO0 (D3) — boot-strap: LOW = bootloader, HIGH = run
- GPIO2 (D4) — boot-strap: must be HIGH at boot, also drives the onboard LED used by heartbeat
- GPIO15 (D8) — boot-strap: must be LOW at boot

Pin assignments are set via `build_flags` in `platformio.ini` (TFT_eSPI requires compile-time defines for inlining). Edit there if your wiring differs.

---

## How it works

A high-level overview is in [ARCHITECTURE.md](ARCHITECTURE.md). In short:

- The device subscribes to `cbpi/fermenterupdate/<your_fermenter_id>`. From that payload, it discovers the IDs of the configured sensor, cooler, and heater.
- It subscribes dynamically to `cbpi/sensordata/<sensor_id>` for live temperature, and to `cbpi/actorupdate/<cooler_id>` briefly to discover the **raw MQTT topic** that drives the relay (`actor/4RB01/R01` or similar).
- Once the raw topic is known, it switches to listening there — that's the real-time, accurate source of cooler state. `actorupdate` itself has stale retained values and isn't trustworthy for live state. See [ARCHITECTURE.md → Decision: raw actor topic](ARCHITECTURE.md#decision-use-the-raw-actor-topic-not-cbpiactorupdate) for the gory details.
- If CBPi4 reassigns the sensor or relay in its UI, the next `fermenterupdate` triggers a re-subscription. No reflash needed.

The entire firmware is non-blocking (no `delay()`, no `while(!connected)`), event-driven, and survives WiFi/MQTT outages cleanly.

An applicative watchdog reboots the device if no fresh MQTT data is received for 181 seconds (= 3 × CBPi4 heartbeat + 1s margin), and performs a preventive reboot every day at 04:00 local time (NTP-synced) to clear any slow memory leaks. Both reboot causes are published on MQTT before restarting, so observers can distinguish a planned restart from a crash. See [External monitoring](#external-monitoring-home-assistant-node-red-etc) below.

---

## External monitoring (Home Assistant, Node-RED, etc.)

The firmware publishes its own state via MQTT, so anything that can speak MQTT can monitor it.

| Topic | Retained | Payload format | When published |
|---|---|---|---|
| `display/<DEVICE_NAME>/status` | yes | `{"value":"online","ts":"UTC 2026-05-18T14:32:11Z"}` | After every successful MQTT connect |
| `display/<DEVICE_NAME>/status` | yes | `{"value":"rebooting","ts":"UTC ..."}` | Before a planned reboot (data-stale or daily) |
| `display/<DEVICE_NAME>/status` | yes | `{"value":"offline","ts":"unknown, set by broker LWT"}` | Auto-published by the broker (LWT) on TCP drop = crash |
| `display/<DEVICE_NAME>/last_reboot_reason` | yes | `{"value":"data_stale","ts":"UTC ..."}` or `{"value":"daily","ts":"UTC ..."}` | Right before a planned reboot |

### Payload format notes

The `ts` field is **always self-describing**:
- **Sync OK**: `"UTC 2026-05-18T14:32:11Z"` — explicit `UTC ` prefix so it's obvious during debug. ISO 8601 format with Zulu suffix for tools that parse it.
- **No NTP sync yet**: `"no NTP sync, uptime=12345ms"` — uptime in milliseconds as fallback. Self-documenting so you immediately know why the field looks weird.
- **Offline (LWT)**: `"unknown, set by broker LWT"` — the LWT payload is registered statically with the broker at connect time, so we can't know in advance when (or whether) it will be published. Observers should use their own reception timestamp for offline events.

To check device state from a shell:

```bash
mosquitto_sub -h <broker> -v -t 'display/+/status' -t 'display/+/last_reboot_reason' --retained-only
```

Output example, healthy device:
```
display/ferm-tornado/status               {"value":"online","ts":"UTC 2026-05-18T14:32:11Z"}
display/ferm-tornado/last_reboot_reason   {"value":"daily","ts":"UTC 2026-05-18T04:00:01Z"}
```

Output, device crashed:
```
display/ferm-tornado/status               {"value":"offline","ts":"unknown, set by broker LWT"}
```

This makes it trivial to integrate later with notification systems (Telegram via Node-RED, Home Assistant alerts, etc.) without firmware changes.

---

## Closed-LAN / no-internet deployments

The firmware was designed to work in fully isolated networks (industrial brewery LAN with no internet access, customer site behind a strict firewall, etc.). Nothing requires reaching the public Internet:

| Function | Internet required? | What happens if blocked |
|---|---|---|
| WiFi association | No | Connects to local AP |
| MQTT to broker | No | Connects to local IP defined in `secrets.h` |
| CBPi4 traffic | No | Talks only to the local broker |
| OTA updates | No | Not implemented in Phase 1 |
| **NTP sync** | **Optionally** | **Daily reboot feature disabled, rest unaffected** |

The only feature affected by lack of internet is the watchdog's daily preventive reboot, which needs wall-clock time. If NTP can't sync (no route to public NTP servers), the firmware:

- Continues running normally
- Logs a one-time warning: `[wdt] WARNING: NTP not synced after 60000 ms uptime — daily reboot disabled until sync`
- Keeps the data-stale watchdog (the more important one) running unchanged

If the customer has a local NTP server (typical on industrial networks — router, PDC, dedicated time server), point at it in `secrets.h`:

```c
#define NTP_SERVER_1   "192.168.1.1"     // your router or local time server
#define NTP_SERVER_2   "192.168.1.2"
```

Or disable the daily reboot entirely if NTP is unavailable and you want clean serial logs:

```c
#define WDT_DAILY_REBOOT_ENABLED 0
```

A device running months on end without the daily reboot is fine in practice — the daily reboot is belt-and-suspenders insurance against slow leaks, not a fundamental requirement.

---

## Project layout

```
.
├── platformio.ini          PlatformIO build config (pinned lib versions)
├── README.md               (this file)
├── ARCHITECTURE.md         technical doc — read this before contributing
├── CHANGELOG.md            release history
├── LICENSE                 GPL-3.0
├── include/
│   ├── config.h            compile-time tunables (pins, timeouts, topics)
│   └── secrets.h.example   credentials template (real one is gitignored)
├── src/
│   ├── main.cpp            orchestration
│   ├── state.{h,cpp}       single source of truth (POD struct)
│   ├── cbpi_proto.{h,cpp}  CBPi4 JSON parsers (host-testable)
│   ├── net_wifi.{h,cpp}    non-blocking WiFi with event handlers
│   ├── net_mqtt.{h,cpp}    async MQTT with dynamic subscription tracking
│   ├── display.{h,cpp}     OLED rendering via U8g2
│   ├── heartbeat.{h,cpp}   LED status patterns + activity pulses
│   └── watchdog.{h,cpp}    "let it crash" applicative watchdog + LWT
└── test/
    ├── Makefile            `make test` to run host-side parser tests
    ├── test_main.cpp       29 unit tests against real CBPi4 payloads
    ├── Arduino.{h,cpp}     minimal Arduino stub for host build
    └── README.md
docs/
└── images/                  README screenshots (real OLED photos)
```

---

## Troubleshooting

### LED is blinking fast and symmetric

WiFi can't associate. Check `WIFI_SSID` and `WIFI_PASS` in `include/secrets.h`. Open `pio device monitor` to see the disconnect reason code:

| Reason | Cause | Fix |
|---|---|---|
| 202 (AUTH_FAIL) | Wrong password | Check `WIFI_PASS` |
| 201 (NO_AP_FOUND) | SSID not visible | Check `WIFI_SSID`, signal range |
| 200 (BEACON_TIMEOUT) | Signal dropping | Move closer, check AP load |

### LED is double-blink slow

WiFi works, MQTT broker rejects the connection. Open the monitor and read the reason code:

| Reason | Cause | Fix |
|---|---|---|
| 5 (NOT_AUTHORIZED) | Bad MQTT credentials | Set `MQTT_USER`/`MQTT_PASS` in `secrets.h` to match your broker config |
| 0 (TCP_DISCONNECTED) | Broker unreachable | Check `MQTT_HOST`/`MQTT_PORT`, network route, broker is running |
| 2 (IDENTIFIER_REJECTED) | Duplicate client ID | Make sure `DEVICE_NAME` is unique across all your boards |

### Upload fails: "Failed to connect to ESP8266" or "Timed out waiting for packet header"

Common causes on Linux:

1. **udev rules not installed** — PlatformIO will warn about this. Install:
   ```bash
   curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core/develop/platformio/assets/system/99-platformio-udev.rules | sudo tee /etc/udev/rules.d/99-platformio-udev.rules
   sudo udevadm control --reload-rules && sudo udevadm trigger
   ```
2. **ModemManager hogging the port** — disable it if you're not using cellular modems:
   ```bash
   sudo systemctl stop ModemManager && sudo systemctl disable ModemManager
   ```
3. **Cheap clone needs slower baud** — drop `upload_speed` in `platformio.ini` from `921600` to `115200`.
4. **D1 mini's onboard USB is dead** — surprisingly common on used boards. Use an external USB-TTL adapter, wire `TX/RX/D3(GPIO0)/GND/5V`, and either hold reset during upload or use the "open minicom → press RESET → close minicom → flash" trick to put the chip in bootloader mode.

### "Cooler shows ON but UI says OFF"

This was a real bug we hit during development. Resolved in v0.4 by listening to the raw actor topic instead of `cbpi/actorupdate/`. See [ARCHITECTURE.md → CBPi4 gotcha](ARCHITECTURE.md#gotcha-cbpiactorupdate-has-stale-retained-values). If you see this again, the symptom likely isn't the firmware but stale retained values on a topic somewhere — sniff with `mosquitto_sub --retained-only -t '#'` to see what's lurking.

### State shows `stale=1` permanently

Means no `fermenterupdate` or `sensordata` arrived in the last 90s. Check:
- `mosquitto_sub -h <broker> -t 'cbpi/fermenterupdate/<your_id>'` — should produce a message at least every 60s
- CBPi4 is running, MQTT is enabled (`mqtt: True` in `config.yaml`)
- Broker is healthy: `systemctl status mosquitto`

### TFT screen blank or showing garbage

1. **Check the wiring**, all 8 wires. SPI is more sensitive to bad contacts than I²C — particularly SCK and MOSI. Reseat dupont connectors firmly. Long flying-wire jumpers (>15cm) can cause issues at 27 MHz — shorten if possible, or lower `SPI_FREQUENCY` in `platformio.ini` to `20000000` (20 MHz).
2. **Check VCC** — most ST7789V breakouts work at 3.3V. Some panels accept 5V on VCC but their data lines stay at 3.3V logic — that's fine. Don't put 5V if the silkscreen says 3.3V only.
3. **Backlight (BLK)** — if VCC is OK but the screen is black, check the BLK pin is wired to 3V3. Some boards integrate a transistor for PWM dimming on BLK; in that case BLK needs a HIGH GPIO not just 3V3.
4. **Wrong driver detection** — if the screen shows garbled colors or shifted pixels, you might have an ST7735 or ILI9341 panel instead of ST7789V. The `ST7789_DRIVER=1` flag in `platformio.ini` is specific. Look at the IC silkscreen on the back of the breakout.
5. **Bring-up debug trick** — to diagnose hardware issues, temporarily add an RGB sweep at the start of `display::tryInit()`:
   ```cpp
   s_tft.fillScreen(TFT_RED);   delay(400);
   s_tft.fillScreen(TFT_GREEN); delay(400);
   s_tft.fillScreen(TFT_BLUE);  delay(400);
   ```
   - **Nothing visible**: backlight off, or SPI wiring wrong (MOSI/SCK/CS)
   - **Colors are swapped** (e.g. red shows blue): swap `TFT_RGB_ORDER` between `TFT_BGR` and `TFT_RGB` in `platformio.ini`
   - **Speckle/noise instead of solid colors**: lower `SPI_FREQUENCY` or shorten the wires

6. **PlatformIO build cache gotcha** — if you change pin assignments via `-D` flags in `platformio.ini` but the upload seems to flash the *old* configuration, run `pio run -t clean -t upload` to force a full rebuild. PlatformIO doesn't always invalidate the build cache on `build_flags` changes — only on source file changes.

### Tests fail locally

```bash
cd test && make clean && make test
```

If a test fails, your local CBPi4 might emit a slightly different payload than what's pinned. Open `test/test_main.cpp` and adjust the `PAYLOAD_*` constants to match what your `mosquitto_sub` dump shows. If it's a real CBPi4 quirk, open an issue with the dump so we can update the upstream tests.

---

## Roadmap

### Phase 1 — D1 mini + display (current)

- [x] Non-blocking WiFi (event-driven, `WiFi.onEvent*`)
- [x] Async MQTT (AsyncMqttClient, dynamic subscription tracking)
- [x] CBPi4 fermenter / sensor / raw actor parsing
- [x] Onboard LED status patterns
- [x] Host-side unit tests (29 cases against real CBPi4 payloads)
- [x] Applicative watchdog (data-stale + daily wall-clock reboot)
- [x] NTP sync (France TZ with DST)
- [x] MQTT LWT with JSON timestamped status payloads
- [x] SSD1306 128×64 I²C rendering (frozen at tag `v0.7.10`)
- [ ] **ST7789V 240×320 SPI rendering in landscape (this branch)** — port in progress, hardware bring-up pending
- [ ] Field test on real fermenter with new screen
- [ ] 3D-printed enclosure (front plate STL)

### Phase 2 — ESP32 + TFT + encoder

- [ ] Port to ESP32 WROOM-32 (`[env:esp32]` in `platformio.ini`)
- [ ] TFT 2.0" 240×320 rendering (TFT_eSPI or LovyanGFX)
- [ ] KY-040 rotary encoder: adjust setpoint ±0.5°C
- [ ] MODE button: toggle AUTO/OFF
- [ ] BACK button: navigation
- [ ] Publish `cbpi/updatefermenter` with new payload to control CBPi
- [ ] Multi-fermenter dashboard (optional, if useful in practice)

See [ARCHITECTURE.md → Phase 2 plan](ARCHITECTURE.md#phase-1-vs-phase-2-plan) for the technical breakdown.

---

## Contributing

Issues and pull requests welcome. A few conventions:

- Run the host-side tests before opening a PR: `cd test && make test` (29 tests must pass).
- Keep modules single-purpose. The clean separation between parsing (`cbpi_proto`), state (`state`), network (`net_wifi`/`net_mqtt`), and rendering (`display`) is what makes the Phase 1 → Phase 2 port realistic.
- If your change contradicts a design decision in [ARCHITECTURE.md](ARCHITECTURE.md), that's fine — but update the doc to reflect the new rationale.
- Code style: 4-space indent, `snake_case` for functions and variables, `PascalCase` for types and enums, namespaces over class hierarchies.
- Commit messages and code comments in English. Issue discussions can be in French or English.

For bug reports, please include:
- Hardware (D1 mini clone, OLED model)
- CBPi4 version (`pip show cbpi`)
- Serial log output during the problematic behaviour
- LED pattern observed
- Output of `mosquitto_sub --retained-only -t '#'` if relevant

---

## Acknowledgements and prior art

- **[PiBrewing/craftbeerpi4](https://github.com/PiBrewing/craftbeerpi4)** — the brewing controller this device complements. Without CBPi4 there's nothing to display.
- **[InnuendoPi/MQTTDevice4](https://github.com/InnuendoPi/MQTTDevice4)** — the inspiration for the CBPi4 MQTT protocol layer. We initially considered forking but went from-scratch (see [ARCHITECTURE.md](ARCHITECTURE.md)). The topic naming conventions and payload formats were verified by reverse-engineering an MQTTDevice4 firmware binary.
- **[marvinroger/async-mqtt-client](https://github.com/marvinroger/async-mqtt-client)** — the non-blocking MQTT library that makes the whole thing reliable.
- **[olikraus/U8g2](https://github.com/olikraus/U8g2)** — pixel-perfect monochrome graphics with a sensible API.

---

## License

GPL-3.0-or-later — see [LICENSE](LICENSE).

Choice of GPL-3.0 is for consistency with the CBPi4 ecosystem (MQTTDevice4 and CBPi4 itself are GPL-licensed).
