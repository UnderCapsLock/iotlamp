# Integration Plan: Home Assistant, HomeKit, Matter

This document outlines how LightPlus can integrate with the major smart-home
ecosystems. It is a plan, not an implementation. The guiding constraint stays
**local-first**: no cloud accounts, no vendor lock-in.

## Current API surface

| Interface | Details |
|---|---|
| WebSocket | `ws://<ip>/ws` — state every 1 s + 12 commands (JSON), optional token auth |
| HTTP | dashboard/PWA, `/wifi` setup page, `/update` (removed — unstable) |
| OTA | espota port 3232 (password) |

Everything needed for integration already exists; ecosystems just need a
protocol adapter.

---

## 1. Home Assistant

### Option A — MQTT discovery ✅ implemented (v1.3.0)

Firmware publishes retained discovery payloads; Home Assistant auto-creates the
entities. Works with any MQTT broker (Mosquitto add-on).

- **Entities**: `light` (on/off, brightness, colour temp, RGB), `binary_sensor`
  (occupancy), `sensor` (illuminance, energy, radar distance), switches for scenes.
- **Topics**
  - `lightplus/<mac>/state` — JSON state (retained)
  - `lightplus/<mac>/set` — JSON commands (same schema as WebSocket commands)
  - `homeassistant/<component>/lightplus_<mac>/config` — discovery payloads
- **Firmware work**: add `PubSubClient` (already installed on the dev machine),
  publish on state change + 10 s heartbeat, subscribe to `set`, reuse
  `wsHandleCommand()` as the shared command dispatcher.
- **Effort**: ~1–2 days. No new hardware.
- **Bonus**: Node-RED, OpenHAB, and HomeSeer can use the same MQTT interface.

### Option B — HACS custom integration (WebSocket client)

A Python integration that connects directly to `ws://<ip>/ws`.

- Full state fidelity, config flow (UI setup), no broker required.
- **Effort**: ~2–3 days (plus HACS publishing overhead).
- Best as a follow-up if MQTT feels heavy for users.

### Option C — ESPHome port

Would give native HA support but discards the custom logic (radar debounce,
schedules, PWA). **Not recommended** — keep the custom firmware.

---

## 2. HomeKit

**HomeSpan** library (Arduino-compatible, supports the classic ESP32).

- Accessories: Lightbulb (On, Brightness, ColorTemperature), OccupancySensor,
  LightSensor. HAP runs locally over WiFi; pairing via QR code.
- **Conflict**: HomeSpan wants port 80, which the dashboard already uses. HAP
  works on any port advertised via mDNS, so co-existence is possible but fiddly.
- **Safer path**: a separate firmware variant (`LightPlus-HomeKit`) built from
  the same `logic.h` + sensor modules, without the web dashboard. Shared core,
  two builds.
- **Effort**: 3–5 days including pairing UX and testing with an iPhone.
- **Limitation**: HomeKit-over-Thread is not possible on the classic ESP32.

---

## 3. Matter

Matter is the strategic end-state (one protocol for Apple, Google, Alexa,
SmartThings), but it **cannot run on the current classic ESP32**: the ESP-Matter
SDK requires an ESP32-C3/C6/S3 with ≥4 MB flash (PSRAM recommended).

- **Hardware revision**: ESP32-C6 — WiFi 6 + Thread radio, cheap (~RM12), and
  doubles as a Thread border router if needed.
- **Clusters to expose**: OnOff, LevelControl, ColorControl (colour
  temperature), OccupancySensing, IlluminanceMeasurement.
- **Commissioning**: QR from the dashboard; production units need a
  Device Attestation Certificate (dev/test certs are free for prototypes).
- **Effort**: 1–2 weeks on new hardware, plus re-certification of the power
  stages. Treat as a v2.0 project.

---

## Comparison

| Path | Effort | Ecosystem | Hardware change |
|---|---|---|---|
| MQTT → Home Assistant | 1–2 days | HA, Node-RED, OpenHAB | none |
| HACS integration | 2–3 days | HA | none |
| HomeKit variant (HomeSpan) | 3–5 days | Apple Home | none |
| Matter (ESP32-C6) | 1–2 weeks | Apple, Google, Alexa, SmartThings | new board |

## Recommended sequence

1. **MQTT discovery** — smallest effort, unlocks Home Assistant immediately.
2. **HACS integration** — if broker-free setup matters for users.
3. **HomeKit variant** — only if Apple-first households are a target.
4. **Matter on C6** — bundle with the next hardware revision (also enables
   cheaper BOM and Thread).

## Security notes

- Keep integrations local; MQTT broker credentials + optional TLS.
- The WebSocket token (`WS_TOKEN`) should be set when MQTT/HA are enabled so
  one compromised device cannot drive the lamp.
- Matter commissioning requires a per-device passcode and rotating setup codes.
