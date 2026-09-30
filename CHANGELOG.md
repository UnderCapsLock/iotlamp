# Changelog

All notable changes to LightPlus are documented in this file.
Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versioning follows [SemVer](https://semver.org/).

## [1.7.0] - 2026-09-22

### Added
- **Radar maximum-distance filter** — presence farther than the configured distance (0–600 cm, 0 = off) is ignored, completing the detection window (min + max); adjustable in the dashboard and via `set_max_distance`

## [1.6.0] - 2026-09-22

### Added
- **Radar minimum-distance filter** — presence closer than the configured distance (0–600 cm, 0 = off) is ignored; useful for ceiling mounting (e.g., ignoring objects right below the lamp). Adjustable in the dashboard under Presence sensitivity (`set_min_distance`)

## [1.5.0] - 2026-09-22

### Added
- **Home Assistant "Auto mode" switch** — one tap returns the lamp to automatic presence control (turning it off forces the lamp off); appears automatically via MQTT discovery
- HA sections dashboard (`tools/home-assistant/lightplus-dashboard.yaml`) with lamp, auto switch, presence and environment cards
- Non-blocking radar recovery: a stalled/absent radar no longer blocks the main loop (~4 s per retry before; MQTT commands now answer in ~160 ms even with the sensor offline)

### Fixed
- Connection cycling when both home WiFi and a phone hotspot were saved — home network is preferred, hotspot is the fallback

## [1.4.0] - 2026-09-22

### Added
- **Multiple saved WiFi networks** (up to 3) — the lamp connects to whichever saved network is available, so home WiFi and a demo hotspot can coexist
- **Change WiFi button** in dashboard Settings (WebSocket `wifi_setup`) — starts the `LightPlus-Setup` AP on demand without rebooting or losing the current connection
- Saved-network count in the state broadcast (`wifi_nets`)

## [1.3.0] - 2026-09-22

### Added
- **MQTT / Home Assistant integration** — retained discovery payloads auto-create a light (on/off, brightness, colour temp, RGB), occupancy, light level, target distance and energy entities; availability via LWT; native `{"cmd":...}` also accepted on the command topic
- MQTT settings in the dashboard (enable, host, port, credentials) persisted to NVS; `set_mqtt` WebSocket command
- Device id (`id`) and MQTT status fields in the state broadcast; MQTT status in diagnostics
- `tools/mqtt_test.py` end-to-end MQTT/HA discovery test

### Changed
- Config version 4 with graceful migration (existing settings are preserved on upgrade instead of reset)

## [1.2.0] - 2026-09-22

### Added
- Captive-portal WiFi fallback: if the saved network is unreachable, the lamp starts a `LightPlus-Setup` AP with a `/wifi` setup page (credentials saved to NVS)
- Optional WebSocket auth token (`WS_TOKEN`, empty = open LAN access)
- Configurable presence behaviour: hold time (1–300 s) and "night light" mode that dims to the floor instead of switching off
- Dashboard: EN/Bahasa Malaysia language toggle, WiFi setup banner, LDR fault warning
- Host unit tests for schedule/easing/kelvin logic (`tests/logic_test.cpp`) wired into CI
- Release workflow: tagged builds attach `lightplus-firmware.bin` + `littlefs.bin` to GitHub releases
- Docs: `docs/INTEGRATIONS.md` (Home Assistant / HomeKit / Matter plan), `docs/HARDWARE.md` (wiring, power, BOM), `ROADMAP.md`, `CONTRIBUTING.md`, `SECURITY.md`, issue templates

### Changed
- Pure schedule logic extracted to `logic.h` (shared by firmware and host tests)
- Radar "online" state now follows the frame stream, not just init
- Dark threshold, presence behaviour, WiFi credentials and energy all persist in NVS (config version 3)

### Fixed
- WebSocket command buffer no longer writes a NUL past the received frame (`data[len] = 0` removed; length-aware JSON parsing)
- WiFi reconnect attempts are rate-limited instead of blocking the main loop for 15 s per iteration

## [1.1.0] - 2026-09-21

### Added
- Split web app (`index.html`, `app.css`, `app.js`, `sw.js`, PWA manifest, icons) served from LittleFS
- PWA support: installable app shell, offline cache, install button, app icons
- Sensors & diagnostics panel (radar status, frames, distance, signal, OUT pin, RX bytes, uptime, firmware version)
- Radar stream watchdog: detects stalled data and restarts the sensor automatically
- Radar wiring auto-detect: probes RX pins at boot and adapts if TX/RX are swapped
- Presence hold (6 s) and LDR hysteresis to prevent on/off blinking
- Wi-Fi country configuration at boot (supports channels 12–13)
- `tools/test_ws.mjs` end-to-end WebSocket test suite
- GitHub Actions firmware compile check
- Vendored `MyLD2410` library under `lib/`
- Branding & logo support: drop `data/logo.png` to brand the dashboard

### Changed
- Rebranded from "IoT Smart Lamp" to **LightPlus**
- Sleep timer now fades to off with a fixed 30-second ramp
- OTA hostname changed to `lightplus`
- Web app is served at `/` (`index.html` default file)

### Fixed
- Failed OTA sessions could freeze the lamp (`ota_in_progress` was never cleared)
- Sleep timer could snap back to full brightness when forced on
- Radar brown-out/stall was never recovered until reboot

## [1.0.0] - 2026-07-25

### Added
- Initial firmware: LD2410C presence detection, LDR ambient sensing, WS2812 ring driver
- WebSocket dashboard, OTA updates, energy tracking, duration-based scheduling, sleep timer
- NVS-backed configuration with validation
