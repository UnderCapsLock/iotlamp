# LightPlus Roadmap

Guiding decisions:

- The lamp is **ceiling-mounted and auto-first**: no physical controls; behaviour
  is presence + ambient light + schedules.
- **Local-first**: no cloud dependency, no accounts.
- Target: competition demo quality now, product quality next.

## Done (v1.2.0)

- [x] Presence-aware auto mode with debounce, hold and LDR hysteresis
- [x] Bedtime dim + wake ramp (duration-based scheduling, midnight safe)
- [x] PWA dashboard: scenes, radar visual, ambient tint, schedule timeline, i18n (EN/MS)
- [x] Captive-portal WiFi fallback (`LightPlus-Setup` AP + `/wifi` page)
- [x] Optional WebSocket auth token (`WS_TOKEN`)
- [x] Configurable presence behaviour: hold time + "night light" instead of off
- [x] Radar stream watchdog + RX/TX auto-detect
- [x] Host unit tests for schedule/easing/kelvin logic (CI)
- [x] CI compile check + tagged release workflow (firmware + filesystem artifacts)

## Next (product hardening)

- [ ] MQTT discovery for Home Assistant (see `docs/INTEGRATIONS.md`)
- [ ] Radar auto-threshold calibration from the dashboard
- [ ] Presence + energy history (24 h charts)
- [ ] Editable/persisted scenes (NVS)
- [ ] In-app onboarding wizard (QR to setup AP)
- [ ] Boot self-test report surfaced in diagnostics
- [ ] Robust in-browser OTA (streaming, chunked; current attempt removed as unstable)

## Hardware / cost (see `docs/HARDWARE.md`)

- [ ] Custom PCB: ESP32-C3 module + regulator + JST connectors (target BOM ≈ RM25–30)
- [ ] Dual-white CCT LED variant (cheaper, higher CRI, lower current than 12× WS2812)
- [ ] Level shifter for LED data (reliability)
- [ ] INA219 shunt for real energy measurement (or drop the energy feature)
- [ ] Integrated 5 V AC-DC module (single-cable product)

## Ecosystem

- [ ] Home Assistant via MQTT (v1), then optional HACS integration
- [ ] HomeKit variant (HomeSpan) if Apple-first users are a target
- [ ] Matter on ESP32-C6 (v2.0, new hardware revision; enables Thread)

## Repo / process

- [ ] Wiring diagram + BOM with live pricing
- [ ] Release notes automation verification on first tag
- [ ] More host tests (config validation, presence latch behaviour)
