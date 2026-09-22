# Tools

## `test_ws.mjs` — WebSocket test suite

Verifies a running LightPlus device end-to-end over the network:

- serves the dashboard shell and checks the LittleFS copy matches `data/`
- connects to `ws://<ip>/ws` and validates the state broadcast
- exercises every command's validation/error path
- runs reversible write tests (override, brightness, CCT, RGB, sleep timer) and restores the device state

```bash
node tools/test_ws.mjs 192.168.0.5
```

The device IP defaults to `192.168.0.5`. Exits non-zero on failure, so it works in scripts or CI (against a reachable device).

To build and flash the filesystem image manually:

```bash
mklittlefs -c data -s 0x20000 -p 256 -b 4096 littlefs.bin
esptool --chip esp32 --port <port> --baud 921600 write-flash 0x3D0000 littlefs.bin
```

## `mqtt_test.py` — Home Assistant/MQTT integration test

Verifies discovery payloads, availability, state publishing and the HA light
command schema against a broker, then cleans up retained test messages.

```bash
pip install paho-mqtt
python tools/mqtt_test.py <broker> <device-id>
```

Device ids look like `lpb9c9fc` (from the state broadcast `id` field).
