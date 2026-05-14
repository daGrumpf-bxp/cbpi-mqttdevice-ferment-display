# Changelog

All notable changes to this project. Format based on [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Added
- **Watchdog module** (`watchdog.{h,cpp}`) implementing the "let it crash" philosophy:
  - Reboots if no fresh MQTT data is received for 181s (= 3× CBPi4 heartbeat + 1s margin). Indicates upstream failure (broker/WiFi/CBPi4 down) that we can't fix locally.
  - Daily preventive reboot at 04:00 local wall-clock time (NTP-synced). Clears slow leaks, lwIP state weirdness. Never lands during a brewing session because wall-clock-anchored, not uptime-anchored.
  - Grace period of 5 minutes at boot — never reboots before that, prevents flapping loops during initial WiFi/MQTT connect.
  - Logs `ESP.getResetReason()` at boot so the previous reboot cause is preserved in serial logs.
- **NTP sync** kicked off in `net_wifi::onGotIp()` callback. Uses `pool.ntp.org` + `time.nist.gov`, France timezone (`CET-1CEST,M3.5.0,M10.5.0/3`) with automatic DST handling.
- **MQTT Last Will & Testament** (LWT):
  - Topic `display/<DEVICE_NAME>/status` carries `online` / `rebooting` / `offline` (latter set by broker on TCP drop).
  - Topic `display/<DEVICE_NAME>/last_reboot_reason` (retained) carries the cause of the previous planned reboot (`data_stale`, `daily`).
  - Allows external monitors (Home Assistant, Node-RED) to detect device state without polling.
- **`net_mqtt::publishStatus()`, `publishLastRebootReason()`, `shutdownClean()`** — public helpers used by the watchdog to publish state transitions and perform clean disconnects (which suppress the LWT trigger).

### Changed
- `display::begin()` now performs a real I²C ACK probe (`Wire.beginTransmission` + `Wire.endTransmission`) instead of trusting U8g2's `begin()` return value, which is unreliable across U8g2 versions and OLED clones. The serial log now distinguishes `init OK` from `no I2C ACK at 0xXX (err=N) — wiring/power?`.
- `display::loop()` now retries init every 5 seconds if the OLED was not detected at boot. This lets you hot-plug the OLED while the device is running, instead of having to reboot. A successful recovery logs `OLED recovered after retry`.
- `state::g.display_ready` flag added — tracks whether the OLED is currently usable. Shown in the periodic state dump (`display=ok` / `display=MISSING`).
- `I2C_CLOCK_HZ` constant added to `config.h` (default 100 kHz). Explicit configuration helps when debugging long cable runs that need to slow down to 50 kHz.
- `display::begin()` now performs a clear+flush self-test as part of init. If the bus is flaky, the failure shows up here rather than at the first real draw.

### Why
v0.5 field test on Pierre's bench revealed the firmware happily logged `[disp] SSD1306 init OK` even when the OLED was missing or had a flaky wire. The actual symptom was a one-off flash at boot then nothing, with no diagnostic feedback. Probing the I²C bus directly is the only portable way to confirm a peripheral is actually there.

The watchdog adds resilience for the unattended operation case: this device will be on a wall for months, and a recoverable hang (TCP zombie connection, lwIP memory leak, etc.) should self-heal in <4 minutes rather than require a manual power cycle. The "let it crash" philosophy is appropriate here because the device is idempotent — CBPi4 is the source of truth, we just display it, so a reboot loses nothing.

## [0.5.0] — 2026-05-14

Initial public release.

Pre-release iterations (v0.1 to v0.4) were done in private during initial development against a real CraftBeerPi4 install (fermenter "Tornado"). The summary below documents what each iteration brought, to provide context for design decisions visible in the code.

### Iteration history

#### v0.5 (current)
- Bumped `DATA_STALE_MS` from 30s to 90s after observing CBPi4 publishes `fermenterupdate` at two cadences: event-driven (~1s on UI changes) plus periodic heartbeat every 60s. 30s threshold produced false-positive staleness flags between heartbeats.
- Documented CBPi4 push cadence behaviour in README and ARCHITECTURE.

#### v0.4
- **Critical bug fix**: cooler/heater state was incorrect (showed ON while CBPi4 UI showed OFF). Root cause: `cbpi/actorupdate/*` is published with `retain=true` and its `state` field doesn't reflect real-time state during AUTO regulation. Investigated via `mosquitto_sub --retained-only` dump.
- Rewrote actor state tracking: use `cbpi/actorupdate/<id>` only to discover `props.Topic` (the raw MQTT topic CBPi pushes to the relay bridge), then subscribe to that raw topic for real-time state. `state` field of `actorupdate` is now intentionally ignored.
- Added `cbpi::parseActorRaw()` parser for the raw topic payload format (`{"state": "on"/"off"}` — string, not boolean).
- Test suite grew from 22 to 29 cases.

#### v0.3
- Documentation pass: troubleshooting tables with action columns, "Patterns de panne fréquents" section cross-referencing LED patterns with serial codes.
- Improved comments in `secrets.h.example` (`DEVICE_NAME`, `MQTT_USER`/`MQTT_PASS` clarified after a NOT_AUTHORIZED diagnostic).

#### v0.2
- Added `heartbeat` module: onboard LED status patterns (BOOT/NO_WIFI/NO_MQTT/READY) with overlay pulses on every MQTT message received. Enables visual status diagnostic without monitoring serial.
- Documented LED patterns in README.

#### v0.1
- Initial firmware: WiFi + MQTT + CBPi4 fermenter/sensor/actor parsing + SSD1306 rendering.
- Modular structure: `state` / `cbpi_proto` / `net_wifi` / `net_mqtt` / `display`.
- 22 host-side unit tests against real CBPi4 payloads (extracted from a `mosquitto_sub` dump on Pierre's broker).
- Project documentation: README with install/build/flash instructions, troubleshooting tables.

### Architecture decisions (cumulative)

- Built from scratch instead of forking InnuendoPi/MQTTDevice4. See [ARCHITECTURE.md → from-scratch decision](ARCHITECTURE.md#decision-from-scratch-not-a-fork-of-mqttdevice4).
- AsyncMqttClient (event-driven) over PubSubClient (blocking). See [ARCHITECTURE.md → MQTT lib choice](ARCHITECTURE.md#decision-asyncmqttclient-over-pubsubclient).
- U8g2 over Adafruit_SSD1306 for display. See [ARCHITECTURE.md → display lib choice](ARCHITECTURE.md#decision-u8g2-over-adafruit_ssd1306).
- Single source of truth in `state::g`, mutated by parsers, read by display. No event queue, no observer pattern.
- Host-testable parsers: `cbpi_proto.cpp` has zero Arduino dependency and is exercised by 29 unit tests on Linux without hardware.

[Unreleased]: https://github.com/daGrumpf-bxp/cbpi-mqttdevice-ferment-display/compare/v0.5.0...HEAD
[0.5.0]: https://github.com/daGrumpf-bxp/cbpi-mqttdevice-ferment-display/releases/tag/v0.5.0
