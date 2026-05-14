# Changelog

All notable changes to this project. Format based on [Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

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
