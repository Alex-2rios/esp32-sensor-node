# ESP32 sensor node

[![ci](https://github.com/Alex-2rios/esp32-sensor-node/actions/workflows/ci.yml/badge.svg)](https://github.com/Alex-2rios/esp32-sensor-node/actions/workflows/ci.yml)

An ESP32 reading temperature, humidity and light, serving its own dashboard over WiFi. No cloud,
no broker, no app. You point a browser at the board and it answers.

![Wiring diagram](docs/wiring.svg)

## What it does

- samples a DHT22 and an LDR every 5 seconds
- keeps the last 120 samples in a ring buffer in RAM
- serves a single page dashboard from LittleFS with a live chart drawn on a canvas
- exposes a small JSON API so anything else can scrape it
- announces itself over mDNS, so it is reachable at `http://sensor-node.local` without hunting
  through the router for its IP
- reconnects on its own when the WiFi drops, and reboots if it cannot get back after 20 seconds

## API

| Endpoint | Returns |
|---|---|
| `GET /` | the dashboard |
| `GET /api/telemetry` | latest reading plus uptime, free heap, RSSI, sample and error counters |
| `GET /api/history` | the whole ring buffer, oldest first |
| `GET /api/health` | 200 when there is a valid reading, 503 when there is not |

```json
{
  "device": "sensor-node",
  "uptime_s": 4127,
  "free_heap": 241080,
  "rssi_dbm": -61,
  "samples": 825,
  "read_errors": 3,
  "reading": { "temperature_c": 24.6, "humidity_pct": 51.2, "light_pct": 38.4, "age_s": 2 }
}
```

The health endpoint returning 503 instead of a JSON error is deliberate. Anything that speaks
HTTP checks can watch this node without parsing a body, which is how it ends up as a target in
my monitoring setup.

## Flashing it

Built with PlatformIO. Copy the config template and fill in your network:

```bash
cp include/config.h.example include/config.h
```

Then the firmware and the filesystem image are two separate uploads:

```bash
pio run -t upload
pio run -t uploadfs
pio device monitor
```

Forgetting the second one is the classic mistake. The board comes up fine, the API answers, and
`/` returns a 500 telling you the filesystem is empty. `include/config.h` is gitignored, so the
credentials never end up in the repo.

The build is checked in CI on every push, along with cppcheck static analysis. Current footprint
on an ESP32 DevKit v1:

```
RAM:   [=         ]  14.7% (used 48284 bytes from 327680 bytes)
Flash: [======    ]  65.0% (used 851629 bytes from 1310720 bytes)
```

Two thirds of the flash is the WiFi stack and the web server, not my code. Worth knowing before
planning OTA updates, which need room for two copies of the firmware at once.

Wiring, pin map and the parts list are in [docs/wiring.md](docs/wiring.md).

## What I learned

- ADC2 is unusable while WiFi is running. The reading either blocks or comes back as noise.
  Moving the LDR to GPIO34 on ADC1 fixed it, and that is the kind of thing you only find by
  measuring rather than by reading the datasheet.
- The default ADC range tops out around 1.1 V, so a 3.3 V divider reads 4095 for most of its
  travel until you set `ADC_11db` attenuation.
- No `delay()` anywhere in `loop()`. The web server has to keep answering while the sampler
  waits for its interval, so both run off `millis()` comparisons. The first version used
  `delay(5000)` and the dashboard timed out on every other request.
- Counting failed reads instead of ignoring them turned out to be the most useful debugging
  feature. A sensor that fails 3 times out of 800 is fine, one that fails 300 times has a wiring
  problem, and you cannot tell those apart if you silently retry.
- `JsonDocument` in ArduinoJson 7 sizes itself, which is a lot less fragile than the fixed
  `StaticJsonDocument` capacity everyone gets wrong on version 6.

## Next

Push the same readings to the Prometheus setup in my monitoring lab so there is history beyond
what fits in RAM, and move the node to deep sleep between samples to see how long it survives
on a battery.
