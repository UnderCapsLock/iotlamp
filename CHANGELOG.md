# Changelog

All notable changes to LightPlus are documented in this file.
Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versioning follows [SemVer](https://semver.org/).

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
