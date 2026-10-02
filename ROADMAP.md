# LightPlus Roadmap

Guiding decisions:

- The lamp is **ceiling-mounted and auto-first**: no physical controls; behaviour
  is presence + ambient light + schedules.
- **Local-first**: no cloud dependency, no accounts.
- Status: competition-complete (🏆 first place) — next is product hardening.

## Done (v1.7.0)

- [x] Presence-aware auto mode with debounce, hold and LDR hysteresis
- [x] Bedtime dim + wake ramp (duration-based scheduling, midnight safe)
- [x] PWA dashboard: scenes, radar visual, ambient tint, schedule timeline, i18n (EN/MS), night mode
- [x] Live sensor view: radar sweep, distances, signal strength; energy tracking
- [x] Presence tuning: five sensitivity presets + detection window (min/max distance)
- [x] Configurable presence behaviour: hold time + "night light" instead of off
- [x] Captive-portal WiFi fallback (`LightPlus-Setup` AP + `/wifi` page), up to 3 saved networks, Change WiFi button
- [x] Radar stream watchdog + boot-time RX/TX auto-detect (self-recovering)
- [x] Home Assistant via MQTT discovery: light, auto-mode switch, occupancy, light level, distance, energy
- [x] Android app (WebView wrapper, one-command build)
- [x] One-click Windows tools: `check.bat` (health check + auto-fixes) and `dashboard.bat`
- [x] Test suites: host logic tests (CI), WebSocket end-to-end (51 checks), MQTT/HA, whole-system health check
- [x] CI compile check + tagged release workflow (firmware + filesystem artifacts)
- [x] Documentation: README, user guide, hardware/BOM, integration plan, changelog

## Next (product hardening)

- [ ] **App auto-discovery** (mDNS service + UDP broadcast; design ready, work-in-progress stashed) — zero-typing setup on mobile
- [ ] HACS integration (WebSocket client, broker-free HA setup)
- [ ] Radar auto-threshold calibration from the dashboard
- [ ] Presence + energy history (24 h charts)
- [ ] Editable/persisted scenes (NVS)
- [ ] In-app onboarding wizard (QR to setup AP)
- [ ] Boot self-test report surfaced in diagnostics
- [ ] Robust in-browser OTA (streaming, chunked; earlier attempt removed as unstable)

## Hardware / cost (see `docs/HARDWARE.md`)

- [ ] Custom PCB: ESP32-C3 module + regulator + JST connectors (target BOM ≈ RM25–30)
- [ ] Dual-white CCT LED variant (cheaper, higher CRI, lower current than 12× WS2812)
- [ ] Level shifter for LED data (reliability)
- [ ] INA219 shunt for real energy measurement (or drop the energy feature)
- [ ] Integrated 5 V AC-DC module (single-cable product)

## Ecosystem

- [x] Home Assistant via MQTT discovery
- [ ] HomeKit variant (HomeSpan) if Apple-first users are a target
- [ ] Matter on ESP32-C6 (v2.0, new hardware revision; enables Thread)

## Repo / process

- [ ] Wiring diagram image + BOM with live pricing
- [ ] More host tests (config validation, presence latch behaviour)
