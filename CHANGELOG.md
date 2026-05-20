# Changelog

All notable changes to this project. Format based on [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Changed
- **Defer the "online" publish until NTP has synced**, so the retained payload on the broker carries a clean UTC timestamp from the very first byte. Falls back to publishing with the uptime-based ts after `NTP_SYNC_WARN_MS` (60s) for closed-LAN deployments where NTP is unreachable. Simpler than the alternative we briefly considered (publish immediately then re-publish on NTP sync), which would have required tracking transitions and managing two retained values for the same state. The current approach uses 6 lines in `loop()`.
- **Status / reboot-reason payloads are now self-describing JSON with timestamps.** Format: `{"value":"<status>","ts":"<timestamp>"}`. The `ts` field is always explicit about its nature: `"UTC 2026-05-18T14:32:11Z"` when NTP is synced, `"no NTP sync, uptime=12345ms"` when NTP hasn't responded yet, or `"unknown, set by broker LWT"` for the offline LWT payload (which is static and can't carry a real timestamp). The `UTC ` prefix is deliberate — readability during debug beats compactness, and avoids the "wait, is Z really UTC?" moment when reading logs.
- LWT offline payload also moved to JSON for format consistency. The `ts` is marked as unknown because MQTT 3.1.1 LWT is statically registered with the broker at connect time — observers wanting a real timestamp for offline events should use their own reception timestamp (this is exactly what an upcoming supervision dashboard will do — see roadmap).

### Fixed
- **TZ string was ignored on some ESP8266 Arduino core versions** — symptom was the daily reboot firing at `WDT_DAILY_REBOOT_HOUR` UTC instead of local time (e.g. scheduled at 04:00 French time fired at 06:00 CEST). Replaced `configTime(tz, ntp1, ntp2)` with the portable `configTime(0, 0, ntp1, ntp2)` + `setenv("TZ", NTP_TZ, 1)` + `tzset()` POSIX dance, which works on every libc-based ESP core. The NTP-synced log line now prints both UTC and local time so timezone misconfiguration is visible immediately rather than hours later when the reboot fires at the wrong hour.
- **Watchdog reboot reason was lost when MQTT was unreachable.** The data-stale watchdog fires precisely when MQTT is unreachable (otherwise messages would keep arriving and reset the stale timer), so `publishRebootIntent()` was silently skipping publishes ("publishStatus skipped: not connected") and the device looked indistinguishable from a brutal crash on the broker side. Fixed by persisting the reboot reason in ESP8266 RTC user memory (12 bytes, survives software reset as long as power is maintained). At the next boot, after MQTT reconnects, the recovered reason is published retained on `display/<DEVICE_NAME>/last_reboot_reason`. Daily reboots (where MQTT is usually still up) publish via both paths — immediate MQTT for live observers, RTC fallback for resilience.

### Added
- `watchdog::flushPendingMqtt()` — called by `net_mqtt` after MQTT connects to publish a recovered RTC-persisted reboot reason.
- **Closed-LAN deployment support**:
  - `NTP_SERVER_1`, `NTP_SERVER_2`, `NTP_TZ` can be overridden in `secrets.h` to point at local time sources (typical on industrial networks).
  - `WDT_DAILY_REBOOT_ENABLED` flag — set to `0` in `secrets.h` to disable the daily preventive reboot entirely (for deployments where NTP truly isn't available or wanted).
  - One-time NTP-not-synced warning in serial log after 60s of uptime if the clock is still invalid. Makes the "running without NTP" state visible rather than silent.
- **Display UX polish based on field testing**:
  - `W` / `M` connectivity icons → full `WIFI` / `MQTT` words (more readable, plenty of horizontal space)
  - Vertical-chevron-looking arrow glyph → plain `->` horizontal arrow (was ambiguous: looked like "heat up" symbol)
  - `cool` / `COOL *` → `not cool` / `COOLING *` (verb-form labels, harder to misread)
  - New `MANUAL` mode label, shown when fermenter.state is OFF but a relay is actually energized (= someone forced the actor on via CBPi UI bypassing fermenter logic). Important to surface so operators don't assume "OFF means safe / nothing's running".
- **Documentation polish**:
  - 4 real OLED photos in `docs/images/` (OFF / AUTO idle / AUTO cooling / MANUAL states), embedded in README.
  - Hardware-validated status badge.
  - Closed-LAN deployment section in README with dependencies table.

### Why
Pierre's first field deployment will be in a brewery LAN with limited or controlled outbound traffic. NTP was hard-coded to public servers (`pool.ntp.org`) which would silently fail and silently disable the daily reboot — bad ergonomics for closed-LAN. Making it explicit and configurable is a 30-line change that opens up real production deployment.

The watchdog-vs-MQTT bug was caught during real field testing: after a 3-minute mosquitto outage, the device rebooted but published nothing — looking exactly like a crash. RTC persistence is the textbook fix and brings observability parity with healthy reboots.

## [0.7.0] — 2026-05-15

First public release. Phase 1 firmware feature-complete: stable Wemos D1 mini + SSD1306 build, field-validated against a real CBPi4 install.

### Iteration history

#### v0.7 (this release)
- **Watchdog module** (`watchdog.{h,cpp}`) implementing the "let it crash" philosophy:
  - Reboots if no fresh MQTT data is received for 181s (= 3× CBPi4 heartbeat + 1s margin).
  - Daily preventive reboot at 04:00 local wall-clock time (NTP-synced).
  - Grace period of 5 minutes at boot (anti-flapping).
  - Logs `ESP.getResetReason()` at boot for post-mortem.
- **NTP sync** kicked off in `net_wifi::onGotIp()`, France TZ with automatic DST.
- **MQTT Last Will & Testament** for external observability: `online`/`rebooting`/`offline` on `display/<DEVICE_NAME>/status`, reboot cause on `display/<DEVICE_NAME>/last_reboot_reason`.
- **`net_mqtt::publishStatus()`, `publishLastRebootReason()`, `shutdownClean()`** public helpers for clean MQTT-aware reboots.

#### v0.6
- **OLED hot-plug detection**: `display::begin()` now performs a real I²C ACK probe (`Wire.beginTransmission` + `Wire.endTransmission`) instead of trusting U8g2's `begin()`. Distinguishes `init OK at 0xXX` from `no I2C ACK at 0xXX (err=N) — wiring/power?`.
- `display::loop()` retries init every 5s if the OLED was missing at boot. Branching an OLED at runtime triggers `OLED recovered after retry`.
- New `state::g.display_ready` flag shown in periodic dump (`display=ok` / `display=MISSING`).
- `I2C_CLOCK_HZ = 100000` made explicit (lowerable to 50000 for noisy long cables).
- Init now does a clear+flush self-test, so flaky-bus failures surface at boot rather than at the first draw.

#### v0.5
- Bumped `DATA_STALE_MS` from 30s to 90s after observing CBPi4 publishes `fermenterupdate` at two cadences: event-driven (~1s on UI changes) plus periodic heartbeat every 60s. 30s produced false-positive staleness flags.
- Documented CBPi4 push cadence in README and ARCHITECTURE.

#### v0.4
- **Critical bug fix**: cooler/heater state was incorrect (showed ON while CBPi4 UI showed OFF). Root cause: `cbpi/actorupdate/*` is published with `retain=true` and its `state` field doesn't reflect real-time state during AUTO regulation. Investigated via `mosquitto_sub --retained-only` dump.
- Rewrote actor state tracking: use `cbpi/actorupdate/<id>` only to discover `props.Topic` (the raw MQTT topic CBPi pushes to the relay bridge), then subscribe to that raw topic for real-time state. `state` field of `actorupdate` is intentionally ignored.
- Added `cbpi::parseActorRaw()` parser for the raw topic payload format (`{"state": "on"/"off"}` — string, not boolean).
- Test suite grew from 22 to 29 cases.

#### v0.3
- Documentation pass: troubleshooting tables with action columns, cross-referenced LED patterns with serial codes.
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
- AsyncMqttClient (event-driven) over PubSubClient (blocking).
- U8g2 over Adafruit_SSD1306 for display.
- Single source of truth in `state::g`, mutated by parsers, read by display.
- Host-testable parsers: `cbpi_proto.cpp` has zero Arduino dependency and is exercised by 29 unit tests on Linux without hardware.
- "Let it crash" watchdog philosophy: this device is idempotent (CBPi4 owns state), so reboot is the cheap recovery option.

[Unreleased]: https://github.com/daGrumpf-bxp/cbpi-mqttdevice-ferment-display/compare/v0.7.0...HEAD
[0.7.0]: https://github.com/daGrumpf-bxp/cbpi-mqttdevice-ferment-display/releases/tag/v0.7.0
