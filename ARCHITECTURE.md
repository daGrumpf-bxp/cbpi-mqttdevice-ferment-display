# Architecture

Technical documentation of `cbpi-mqttdevice-ferment-display`. This file explains **why** the code is structured the way it is, what alternatives were considered, and what gotchas were discovered along the way.

For end-user docs (how to install, flash, debug), see [README.md](README.md).

---

## Table of contents

1. [Big picture](#big-picture)
2. [Module-by-module overview](#module-by-module-overview)
3. [Data flow: from CBPi4 to OLED](#data-flow-from-cbpi4-to-oled)
4. [Discovery dance: how the firmware learns its environment](#discovery-dance-how-the-firmware-learns-its-environment)
5. [CBPi4 glossary and observed semantics](#cbpi4-glossary-and-observed-semantics)
6. [Key technical decisions](#key-technical-decisions)
7. [Known limitations and rationale](#known-limitations-and-rationale)
8. [Phase 1 (ESP8266) vs Phase 2 (ESP32) plan](#phase-1-vs-phase-2-plan)

---

## Big picture

This firmware turns a **Wemos D1 mini** (ESP8266) or **ESP32** into a wall-mounted display next to a fermenter, showing in real-time:

- Current temperature (from the sensor configured on the fermenter)
- Target temperature (setpoint)
- Mode (AUTO / OFF — CBPi4 fermenters are binary)
- Cooler/heater state (whether the relay is currently energized)

The brain stays in **CraftBeerPi4** — this device is just a peripheral that listens on MQTT and renders. No control logic, no PID, no hysteresis here. CBPi4 owns the state; we display it.

### Why a separate display device at all

Three reasons:

1. **Glance-ability** — walking into the fermentation room and seeing 4 OLED screens at 2m distance beats pulling out a phone, opening CBPi4 UI, scrolling to the right fermenter.
2. **Resilience boundary** — high-voltage (220V relay boards) and low-voltage (display + buttons) are physically separated. The display device lives at the front, never carries mains.
3. **Future bidirectional UX** — Phase 2 will add a rotary encoder + buttons for local control (setpoint, mode toggle). Operators interact with the cuve directly, not through a phone.

---

## Module-by-module overview

Code lives in `src/`, headers in `src/` (Arduino convention) and `include/` (config + secrets only). Each module is a tight namespace exposing `begin()` and `loop()`.

```
┌─────────────────────────────────────────────────────────────────┐
│                            main.cpp                             │
│  orchestrator: setup() inits every module, loop() ticks them    │
└──┬───────────┬───────────┬────────────┬────────────┬────────────┘
   │           │           │            │            │
   ▼           ▼           ▼            ▼            ▼
┌──────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌───────────┐
│ net_ │  │  net_   │  │ display │  │heartbeat│  │ (periodic │
│ wifi │  │  mqtt   │  │         │  │         │  │  state    │
│      │  │         │  │         │  │         │  │   dump)   │
└──┬───┘  └────┬────┘  └────┬────┘  └────┬────┘  └───────────┘
   │           │            │            │
   │  writes   │ writes     │ reads      │ reads
   ▼           ▼            ▼            ▼
       ┌───────────────────────────────────┐
       │            state::g               │
       │  (single source of truth, plain   │
       │   POD struct, no synchronization  │
       │   needed on single-core ESP8266)  │
       └───────────────────────────────────┘
                       ▲
                       │ parses payloads, mutates state::g
                       │
              ┌────────┴────────┐
              │   cbpi_proto    │
              │  (pure parser,  │
              │   host-testable)│
              └─────────────────┘
```

| Module | Lines | Role |
|---|---:|---|
| `main.cpp` | ~55 | Orchestration. `setup()` initialises modules in order, `loop()` calls each module's `loop()` and a periodic state dump. |
| `state.h/.cpp` | ~110 | Plain-old-data struct holding everything the firmware has learned from CBPi4. Single source of truth. |
| `cbpi_proto.h/.cpp` | ~200 | JSON parsers for CBPi4 payloads. **No Arduino I/O dependency** — testable on host. |
| `net_wifi.h/.cpp` | ~100 | Non-blocking WiFi state machine using `WiFi.onEvent*` callbacks. Logs disconnect reason codes for debugging. |
| `net_mqtt.h/.cpp` | ~210 | Non-blocking async MQTT (AsyncMqttClient) with dynamic subscription tracking — see [Discovery dance](#discovery-dance). |
| `display.h/.cpp` | ~165 | OLED rendering via U8g2. Throttled to 500ms. Designed to be replaceable for Phase 2 TFT. |
| `heartbeat.h/.cpp` | ~130 | Onboard LED status patterns (BOOT/NO_WIFI/NO_MQTT/READY) with activity pulses on MQTT messages. |
| `watchdog.h/.cpp` | ~120 | "Let it crash" applicative watchdog. Reboots on prolonged MQTT silence (181s) or daily at 04:00 local. Publishes intent + reason via MQTT LWT before restarting. |

Total: ~1300 lines of code + ~280 lines of tests + ~400 lines of doc.

---

## Data flow: from CBPi4 to OLED

```
CraftBeerPi4 (Python, on a Pi)
   │
   │ publishes JSON to broker
   ▼
mosquitto MQTT broker
   │
   │ pushes to subscribers
   ▼
D1 mini → AsyncMqttClient onMessage callback (net_mqtt.cpp)
   │
   │ routes by topic prefix
   ▼
cbpi_proto::parseXxx() reads JSON, mutates state::g
   │
   │ (return)
   ▼
heartbeat::pulse()      → LED brief OFF flash
                          (visual "traffic flowing")
   │
   │ (back to main loop)
   ▼
display::loop() (every 500ms)
   │
   │ reads state::g
   ▼
U8g2 framebuffer → I²C → OLED pixels
```

No queue, no event bus. The state mutation happens synchronously in the MQTT callback (which runs in the main loop context on ESP8266). The display reads from `state::g` on its own schedule. **They don't need to be synchronized** because reading a `bool` or `float` on ESP8266 is atomic at the word level, and we never look at multiple fields where atomic consistency between them matters.

On ESP32 (Phase 2), the callback may run on a different core; we'll add a mutex around `state::g` writes at that point.

---

## Discovery dance: how the firmware learns its environment

CBPi4 doesn't expose a "give me everything about fermenter X" API over MQTT. Instead, you piece information together through a chain of subscriptions. Here's the full sequence at boot:

```
T+0    [boot]
       └─ heartbeat begins (LED solid ON)
       └─ display shows "(waiting...)"
       └─ WiFi station mode starts connecting

T+~3s  [WiFi associated, IP via DHCP]
       └─ heartbeat switches to NO_MQTT pattern (double-blink slow)
       └─ MQTT client starts connecting

T+~3s  [MQTT connected]
       └─ heartbeat switches to READY pattern (solid + dip every 2s)
       │
       ├─ subscribe cbpi/fermenterupdate/<FERMENTER_ID>
       │
       └─ publish cbpi/updatefermenter {} ← forces CBPi to re-emit ASAP
                                              instead of waiting 60s

T+~4s  [first fermenterupdate received]
       └─ parseFermenterUpdate extracts:
          • fermenter_name        e.g. "Tornado"
          • sensor_id             e.g. "7eskHbgqTA9FNihrP6AJ59"
          • cooler_id             e.g. "H2uFH5kPh8ttjBvKFhbgpb"
          • heater_id             (often empty for fermenters)
          • target_temp           e.g. 25.0
          • mode                  AUTO or OFF
       │
       └─ resyncDynamicSubscriptions():
          ├─ subscribe cbpi/sensordata/<sensor_id>
          ├─ subscribe cbpi/actorupdate/<cooler_id>    (ephemeral!)
          └─ display now shows fermenter info + (still no temp value)

T+~4.5s [first actorupdate received — possibly stale retained value]
       └─ parseActorUpdate extracts props.Topic
          • cooler_topic   e.g. "actor/4RB01/R01"
       │
       └─ resyncDynamicSubscriptions() detects cooler_topic now set:
          ├─ UNSUBSCRIBE cbpi/actorupdate/<cooler_id>  ← ephemeral done
          └─ subscribe actor/4RB01/R01

T+~4.5s [first sensordata received — possibly retained]
       └─ current_temp populated, display now fully usable

T+~5s  [first raw actor message]
       └─ cooler_on populated from {"state": "on"/"off"}
       └─ display shows COOL state correctly

T+ ongoing
   ├─ sensordata pushed every ~1s            (high frequency)
   ├─ fermenterupdate pushed on event (~1s)
   │  AND periodically every 60s (CBPi heartbeat)
   ├─ raw actor topic pushed only on state change
   │  (no periodic heartbeat — relies on event-driven only)
   └─ each MQTT message triggers heartbeat::pulse()
      (brief LED OFF flash, visible "traffic"
       indicator)
```

### Why this dance?

The naïve approach would be: subscribe to `cbpi/actorupdate/<cooler_id>` and trust its `state` field. **We did this initially**, and it produced incorrect display state. See [decision: raw actor topic](#decision-use-the-raw-actor-topic-not-cbpiactorupdate) for the full rationale.

### What if CBPi4 reassigns the cooler to a different actor?

The next `fermenterupdate` will arrive with a new `cooler_id`. `resyncDynamicSubscriptions()` detects the change and:
- Clears `cooler_topic` (forces re-discovery)
- Unsubscribes from the old raw topic
- Subscribes to `cbpi/actorupdate/<new_cooler_id>` (ephemeral again)
- And so on, the dance restarts.

This means **zero firmware reflash** when CBPi4 config changes. The only thing baked into the firmware is `FERMENTER_ID`.

---

## CBPi4 glossary and observed semantics

CraftBeerPi4 documentation is sometimes thin on the MQTT side. Here's what we observed empirically (May 2026 on CBPi4 4.x).

### Kettle vs Fermenter

CBPi4 distinguishes two regulation logics:
- **Kettle**: ternary mode (`auto`/`manual`/`off`), with PID or hysteresis. Used for mashing, boiling.
- **Fermenter**: **binary mode** (`auto`/`off` only — no "manual"). Used for fermentation control.

`cbpi/fermenterupdate/<id>` payload has a `state` field that is a **boolean**: `true` = AUTO, `false` = OFF. There's no third state.

### MQTTActor

A CBPi4 actor of type `MQTTActor` is a relay/load controlled via MQTT. Its config has a single property `Topic` (the raw MQTT topic CBPi will push to, e.g. `actor/4RB01/R01`). Each MQTTActor also has a CBPi-internal ID (a 22-char random string like `H2uFH5kPh8ttjBvKFhbgpb`).

CBPi publishes two MQTT streams for each actor:
- `cbpi/actorupdate/<actor_id>` — bookkeeping/status, **with `retain=true` and stale semantics**. See gotcha below.
- `<actor.props.Topic>` (e.g. `actor/4RB01/R01`) — real-time commands to the relay bridge, with `retain=true` and accurate semantics.

### Sensor data

Each CBPi sensor publishes to `cbpi/sensordata/<sensor_id>`. Payload:

```json
{"id": "7eskHbgqTA9FNihrP6AJ59",
 "value": 15.875,
 "datatype": "value",
 "inrange": true}
```

`datatype` is always `"value"` for temperature sensors (other types exist for gravity sensors etc.). `inrange` is a CBPi-computed boolean: false when the value falls outside a configured plausibility range (useful to ignore garbage readings).

### Cadence of updates

- **Event-driven**: CBPi publishes ~1 second after any state change (UI toggle, setpoint change, etc.)
- **Periodic heartbeat**: every 60s by default, all fermenter/kettle states are re-published even if unchanged. Configurable via the `MQTTUpdate` CBPi setting.

This is why we set `DATA_STALE_MS = 90s` (60s heartbeat + 30s safety margin).

### Gotcha: `cbpi/actorupdate/*` has stale retained values

Observation (May 2026, real install): the `state` field in `cbpi/actorupdate/<id>` is **not** a reliable real-time indicator. Specifically:

- All `cbpi/actorupdate/*` topics are published with `retain=true`. New subscribers receive whatever was last broadcast, which can be hours or days old.
- CBPi re-publishes `actorupdate` on **manual UI toggle** of the actor, **not** on AUTO regulation. So a cooler being driven on/off by a fermenter's hysteresis logic produces no `actorupdate` traffic.
- Result: at boot, we received `{"state": true}` retained, while the actual relay was off (visible in the CBPi UI). Trusting that boolean displayed wrong info.

**Therefore**: we use `actorupdate` only to discover `props.Topic` (which is stable), then unsubscribe, and listen on the raw topic which CBPi updates on every actual state change.

---

## Key technical decisions

### Decision: from-scratch, not a fork of MQTTDevice4

Considered: forking [InnuendoPi/MQTTDevice4](https://github.com/InnuendoPi/MQTTDevice4) which already speaks CBPi4 MQTT and could be repurposed.

Rejected because:
- MQTTDevice4 is a multi-purpose controller (sensors + actors + induction hob + Nextion HMI). 80% of the code is dead weight for a display-only device.
- Its WiFi/MQTT layer uses **PubSubClient in blocking mode** with `delay()` scattered throughout — the exact pattern Pierre wants to avoid (he had recurring WiFi/MQTT drops on his D1 minis running MQTTDevice4).
- Phase 2 target hardware (ESP32 + TFT 240x320 + rotary encoder) has very little in common with MQTTDevice4's Nextion focus. Porting effort would exceed re-writing.

Kept as inspiration: the CBPi4 topic names and payload formats. Verified by binary-extracting strings from `MQTTDevice4.ino.bin` and `mosquitto_sub` dumps from Pierre's broker.

### Decision: AsyncMqttClient over PubSubClient

`PubSubClient` is the de-facto standard MQTT lib for Arduino/ESP8266, but it's synchronous: `connect()` blocks until the broker responds (or times out), `loop()` must be called frequently or messages are dropped, and the typical reconnect pattern is a blocking `while(!client.connected()) { ... }` that can starve the WiFi stack and trigger the software watchdog.

[AsyncMqttClient](https://github.com/marvinroger/async-mqtt-client) by marvinroger is built on top of Espressif's LwIP callbacks. Everything is event-driven: `connect()` returns immediately, the broker response arrives via `onConnect()`, messages arrive via `onMessage()`, etc. No `loop()` polling needed — the lib hooks into the lwIP TCP callbacks directly.

Trade-off: AsyncMqttClient has slightly more boilerplate to set up (callbacks for everything) and less community familiarity. But for a 24/7 device on a potentially noisy WiFi, it's clearly the right tool.

### Decision: U8g2 over Adafruit_SSD1306

Both work. U8g2 was chosen for:
- **Display-agnostic constructors** — switching from SSD1306 to SH1106 (encountered on some Pierre's AZDelivery 1.3" OLEDs) is a single line change. Same lib supports many ST7565, SH1106, SSD1306, ST7920, etc.
- **Better font handling** — adjustable size, picks fonts on demand, supports 8/16/24-pixel-tall numerals out of the box. Adafruit_SSD1306 only has a single 6x8 font built in.
- **Full framebuffer mode** — clean for our use case (small static layout), saves only ~1KB of RAM.

### Decision: Use the raw actor topic, not `cbpi/actorupdate/*`

See [the gotcha section](#gotcha-cbpiactorupdate-has-stale-retained-values) above. TL;DR: the `state` field in `actorupdate` is not real-time. The raw topic (declared in `actorupdate.props.Topic`) is.

Subscription pattern is therefore a **two-stage dance**:
1. Subscribe to `cbpi/actorupdate/<actor_id>` only to discover `props.Topic`
2. Subscribe to `<props.Topic>` for actual state
3. Unsubscribe from `actorupdate` once Topic is known

If `actor_id` changes (CBPi reconfig), the dance restarts naturally.

### Decision: applicative watchdog with "let it crash" philosophy

Considered: a sophisticated 3-pronged watchdog watching WiFi, MQTT, and data freshness independently, with degraded modes and graceful recovery on each axis.

Rejected because **all three failure modes converge to the same observable**: no fresh MQTT data arriving. WiFi down → no MQTT data. MQTT broker down → no MQTT data. CBPi4 crashed → no MQTT data. So three watchdogs are three implementations of one truth. The simpler model — "if I haven't heard fresh data for 3 × CBPi4 heartbeats, reboot" — covers all the cases the complex version would have, with a fraction of the code.

This works because the device is **idempotent**:
- CBPi4 is the source of truth, this device just displays
- Reboot brings the system to a known-good state in ~3 seconds
- No state is lost on reboot (everything will be re-discovered from MQTT)

So "let it crash" makes sense here in a way it wouldn't for, e.g., a brewing controller that holds setpoints and PID state.

**Parameters chosen**:
- `WDT_DATA_STALE_MS = 181 seconds` = 3 × CBPi4 heartbeat (60s) + 1 second margin. Three full cycles without a single message is unambiguous: something is broken upstream.
- `WDT_MIN_UPTIME_MS = 5 minutes` grace period. Prevents flapping reboot loops if the device boots into a misconfigured network.
- `WDT_DAILY_REBOOT_HOUR = 04:00` local time. NTP-anchored, not uptime-anchored. Never lands during a brewing session because 4am is brewer's sleep time.

**Why the daily reboot exists**: long-running ESP8266 firmwares are vulnerable to slow lwIP memory leaks, retained socket state weirdness, and other gremlins that accumulate over weeks. A clean daily reboot mitigates all of them at the cost of ~5 seconds of downtime at a chosen time.

### Decision: MQTT Last Will & Testament for external observability

The watchdog publishes its reboot intent via MQTT before calling `ESP.restart()`. Three observable states result:
- `online` (retained) — published right after MQTT connect succeeds. External observers see this and know we're alive.
- `rebooting` (retained) — published deliberately before a planned reboot. Observer sees this and knows the upcoming downtime is expected.
- `offline` (retained, set by broker) — fires only when the broker detects we dropped TCP without a clean DISCONNECT, i.e. when we genuinely crashed.

Topics:
- `display/<DEVICE_NAME>/status` — current state (online/rebooting/offline)
- `display/<DEVICE_NAME>/last_reboot_reason` (retained) — `data_stale` or `daily`, so post-mortem is possible

This gives a clean separation between **expected restarts** (we know about them) and **crashes** (we don't). CBPi4 doesn't natively monitor incoming MQTT, but Home Assistant, Node-RED, or a simple `mosquitto_sub` watcher can be added later without firmware changes.

### Decision: NTP sync triggered on WiFi GotIP, not in a dedicated module

The watchdog's daily-reboot feature needs wall-clock time, which means NTP. Considered creating a `net_time.{h,cpp}` module to encapsulate this; rejected because it's literally one function call (`configTime`) and the natural trigger point is when WiFi gets its IP — which is in `net_wifi.cpp`'s `onGotIp` callback already.

```cpp
static void onGotIp(...) {
    state::g.net_status = state::NetStatus::WIFI_OK;
    configTime(NTP_TZ, NTP_SERVER_1, NTP_SERVER_2);
}
```

`configTime` is non-blocking — it just kicks off a background sync. The local time becomes valid after a few seconds. The watchdog's daily-reboot logic checks this explicitly before considering the wall-clock hour valid:

```cpp
time_t now_t = time(nullptr);
if (now_t < 1577836800UL /* 2020-01-01 */) return;  // not synced yet
```

The POSIX TZ string `CET-1CEST,M3.5.0,M10.5.0/3` handles DST automatically for France/Europe, so no code changes needed twice a year.

### Decision: `DATA_STALE_MS = 90 seconds` (display warning, distinct from watchdog)

`DATA_STALE_MS` is for the **display layer**, NOT the watchdog. They're related but separate:
- `DATA_STALE_MS = 90s` triggers a visible `!` warning on the OLED. Tells the operator that something might be off.
- `WDT_DATA_STALE_MS = 181s` triggers a reboot. Tells the firmware to give up and restart.

The 90s threshold for the display warning was chosen because CBPi pushes `fermenterupdate` at two cadences: ~1s on events, plus a periodic heartbeat every 60s. So in steady state, the worst case between two fermenter messages is 60s. We pad to 90s (60s + 30s margin) before showing the warning. Below 60s would flag false positives on every quiet minute; above 120s would hide real outages too long.

### Decision: single header `secrets.h` (gitignored)

Two alternatives were considered:
- WiFiManager + captive portal for runtime config (more polished UX)
- Hardcoded in `config.h`

Rejected WiFiManager for Phase 1 because:
- Adds ~30 KB to the binary and additional library deps
- Requires button/captive portal flow for credential reset — out of scope for a dev iteration
- The device is glued to one location (one fermenter), credentials rarely change

Rejected `config.h` because:
- Mixes board-tuning constants (timeouts, pin numbers — versionable) with secrets (gitignored)
- Awkward to have a single file partially versioned

`secrets.h` is gitignored, `secrets.h.example` is versioned. Standard pattern. Phase 2 may add WiFiManager if we want shared firmware across multiple boards with different IPs.

---

## Known limitations and rationale

### Single fermenter per device

By design. Pierre chose **1 device = 1 fermenter** (see brief) for two reasons:
- The display is physically attached to the fermenter — having a 2nd screen showing a 2nd fermenter on the same physical cuve is weird
- Local control (Phase 2 encoder) needs to target one fermenter unambiguously

This means `FERMENTER_ID` is baked at compile time. To re-target a board, change `secrets.h` and reflash. The CHANGELOG documents this as a one-step reconfiguration.

### No HTTP REST control to CBPi

Phase 2 will pilot CBPi via MQTT (publish to `cbpi/updatefermenter` with a new payload). We deliberately avoid HTTP REST to CBPi for:
- No coupling to the CBPi web server IP/port
- All traffic stays on the broker (a single firewall rule)
- The broker handles retry, QoS, persistence

### Display is throttled to 500ms

`display::loop()` redraws at most twice per second regardless of how fast new MQTT data arrives. Avoids burning CPU on framebuffer + I2C transfer for sensor updates that don't change the visible value much. Plenty fast for human perception.

### No persistence across reboots

Every reboot starts from `(waiting...)` and discovers the environment from scratch. No EEPROM/LittleFS state. Trade-off: more boot traffic vs. simpler code. Boot completes in <5s on a healthy network so it's not a real problem.

---

## Phase 1 vs Phase 2 plan

### Phase 1 — current — Wemos D1 mini + SSD1306 128×64 I²C

- Read-only display
- LED status patterns
- Local dev iteration on what's already at hand
- ~1100 lines of code, fits 30% Flash + 40% RAM on ESP8266

### Phase 2 — ESP32 WROOM-32 + ST7789V TFT 240×320 + KY-040 encoder + 2 buttons

Hardware components ordered, ETA late May 2026.

Code changes anticipated:
- **`net_wifi.cpp`** — ESP32 has different event signatures (`WiFi.onEvent` with generic callback instead of typed `onStationModeXxx`). Small adaptation.
- **`display.cpp`** — full rewrite to use TFT_eSPI or LovyanGFX for the ST7789V. Same `begin()/loop()` API, internal state and layout completely new.
- **`input.{h,cpp}`** — new module for encoder + buttons, with debounce and acceleration. Publishes intent (setpoint change, mode toggle) via a callback or a small intent queue.
- **`net_mqtt.cpp`** — add `publish()` helpers to push `cbpi/updatefermenter/<id>` payloads with new target_temp / state.
- **`state.cpp`** — likely add a mutex around writes if MQTT callbacks land on a different FreeRTOS task than the main loop.

What stays unchanged:
- **`cbpi_proto.cpp`** — pure parser, no hardware. Tests stay green.
- **`heartbeat.cpp`** — LED patterns, just the LED pin changes.
- **`state.h`** — pure data, no Arduino dependency.

The Phase 2 branch will be `feat/esp32-tft`, kept separate until it's verified working on real hardware. The ESP32 binary will be a separate `[env:esp32]` block in `platformio.ini`, so the same git checkout can produce both the D1 mini firmware (for dev) and the ESP32 firmware (for prod), via `pio run -e d1_mini` or `pio run -e esp32`.

---

## Contributing technical changes

If you're a contributor and considering a code change:

1. Check `test/test_main.cpp` first — many parser invariants are pinned there. Run `make test` in `test/` before and after your change.
2. Read the relevant section of this file. If your change contradicts a decision documented here, that's fine — but update the doc to reflect the new rationale.
3. Keep modules single-purpose. The clean separation between parsing (`cbpi_proto`), state (`state`), network (`net_wifi`/`net_mqtt`), and rendering (`display`) is what makes the Phase 1 → Phase 2 port realistic.
