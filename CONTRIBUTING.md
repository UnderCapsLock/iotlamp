# Contributing to LightPlus

Thanks for helping improve LightPlus.

## Development environment

- Arduino IDE 2.x with **esp32 core 3.3.x**
- Or arduino-cli (the repo CI uses it):

```bash
arduino-cli core install esp32:esp32@3.3.11 \
  --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli lib install "FastLED" "ArduinoJson" "ESP Async WebServer"
cp -r lib/MyLD2410 ~/Arduino/libraries/
cp wifi_config.h.example wifi_config.h   # add your WiFi credentials
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=min_spiffs .
```

## Flashing

1. Upload the filesystem (dashboard) — Arduino IDE: *Tools → ESP32 Sketch Data
   Upload*; or manually:
   ```bash
   mklittlefs -c data -s 0x20000 -p 256 -b 4096 littlefs.bin
   esptool --chip esp32 --port <port> --baud 921600 write-flash 0x3D0000 littlefs.bin
   ```
2. Upload the firmware via USB (first time) or OTA (`espota` / Arduino IDE
   network port) afterwards.

## Tests

- Host logic tests (schedule math, easing, kelvin conversion):
  ```bash
  g++ -std=c++17 -Wall -Wextra -o /tmp/logic_test tests/logic_test.cpp && /tmp/logic_test
  ```
- Device test suite (requires a running device):
  ```bash
  node tools/test_ws.mjs <device-ip>
  ```

## Pull requests

- Keep changes focused; one feature per PR.
- Run the host tests and compile before submitting; CI also compiles the
  firmware and runs the host tests.
- Match the existing code style (4-space indent, `snake_case` functions,
  `g_` globals, section comments).
- Update `CHANGELOG.md` for user-visible changes.

## Adding a feature

1. Pure logic (math, scheduling, colour) goes in `logic.h` with host tests in
   `tests/logic_test.cpp`.
2. Runtime state belongs in the `LampState` struct; persisted settings belong
   in `RuntimeConfig` (add a version bump).
3. WebSocket commands are handled in `wsHandleCommand()`; document new commands
   in the README API table.
4. Dashboard UI lives in `data/` (no build step). Keep EN/MS translations in
   `app.js` in sync.
