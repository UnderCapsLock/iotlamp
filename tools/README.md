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

## `healthcheck.py` — whole-system check (or double-click `check.bat`)

One command that verifies the entire deployment: lamp discovery (via MAC suffix
or `lightplus.local`, no IP needed), dashboard, WebSocket state, radar stream,
MQTT broker, lamp MQTT connection, retained broker data, Docker containers and
Home Assistant. Every failure prints the likely cause and how to fix it.
Docker Desktop is started automatically if it isn't running, and if the
laptop's IP changed (DHCP), the lamp's MQTT host is re-pointed automatically.

```bash
check.bat                                        # double-click friendly, pauses at the end
python tools/healthcheck.py
python tools/healthcheck.py --ip 192.168.0.7    # skip discovery
```

Exits non-zero if anything fails — perfect as a pre-demo ritual.

## `mqtt_test.py` — Home Assistant/MQTT integration test

Verifies discovery payloads, availability, state publishing and the HA light
command schema against a broker, then cleans up retained test messages.

```bash
pip install paho-mqtt
python tools/mqtt_test.py <broker> <device-id>
```

Device ids look like `lpb9c9fc` (from the state broadcast `id` field).
