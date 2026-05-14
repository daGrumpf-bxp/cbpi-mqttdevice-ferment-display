---
name: Bug report
about: Something doesn't work as expected
title: '[BUG] '
labels: bug
---

## Environment

- **Hardware**: e.g. Wemos D1 mini clone with CH340 + AZDelivery SSD1306 1.3" SH1106
- **CBPi4 version**: output of `pip show cbpi`
- **MQTT broker**: e.g. mosquitto 2.0.x on Raspbian Bookworm
- **Firmware version / commit**: e.g. v0.5.0 or `git rev-parse --short HEAD`
- **PlatformIO version**: `pio --version`

## Observed behaviour

What you see — be precise (don't say "it's broken", describe what's actually on the screen / LED / serial).

## Expected behaviour

What you expected to see.

## Serial log

Paste output from `pio device monitor` showing the problematic moment. Trim irrelevant lines but include at least 10 seconds of context around the issue.

```
[paste here]
```

## LED pattern observed

e.g. "Solid ON with brief dip every 2s" (= READY) or "Double-blink slow" (= NO_MQTT). Cross-reference the README LED table.

## MQTT broker dump

If the issue might be CBPi4-side rather than firmware-side, please include relevant output. Useful commands:

```bash
# What's being pushed for your fermenter:
mosquitto_sub -h <broker> -v -t 'cbpi/fermenterupdate/<your_id>'

# All retained values on the broker:
mosquitto_sub -h <broker> --retained-only -t '#' -v -W 1
```

## Steps to reproduce

If you can reproduce on demand, list the steps. If it's intermittent, describe how often / under what conditions.

## Anything else

Any other context (screenshots, hardware photos, network setup, etc.)
