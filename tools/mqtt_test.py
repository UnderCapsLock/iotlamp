#!/usr/bin/env python3
"""MQTT integration test for LightPlus.

Usage: python tools/mqtt_test.py <broker> <device-id> [port]

Verifies Home Assistant discovery payloads, availability, state publishing and
the HA light command schema, then cleans up retained test messages.
Requires: pip install paho-mqtt
"""

import json
import sys
import threading
import time

import paho.mqtt.client as mqtt

BROKER = sys.argv[1] if len(sys.argv) > 1 else "broker.hivemq.com"
DEVID = sys.argv[2] if len(sys.argv) > 2 else None
PORT = int(sys.argv[3]) if len(sys.argv) > 3 else 1883

if not DEVID:
    print("usage: mqtt_test.py <broker> <device-id> [port]")
    sys.exit(2)

PREFIX = f"lightplus/{DEVID}"
DISCOVERY = [
    f"homeassistant/light/{DEVID}_light/config",
    f"homeassistant/binary_sensor/{DEVID}_occupancy/config",
    f"homeassistant/sensor/{DEVID}_light_level/config",
    f"homeassistant/sensor/{DEVID}_distance/config",
    f"homeassistant/sensor/{DEVID}_energy/config",
]
STATE_TOPIC = f"{PREFIX}/ha/state"
AVAIL_TOPIC = f"{PREFIX}/availability"
SET_TOPIC = f"{PREFIX}/ha/set"

received = {}
lock = threading.Lock()
fails = []


def check(ok, msg):
    print(("PASS " if ok else "FAIL ") + msg)
    if not ok:
        fails.append(msg)


def wait_for(topic, timeout=20):
    end = time.time() + timeout
    while time.time() < end:
        with lock:
            if topic in received:
                return received[topic]
        time.sleep(0.3)
    return None


def on_connect(client, userdata, flags, reason_code, properties):
    client.subscribe("homeassistant/#")
    client.subscribe(PREFIX + "/#")
    print(f"subscribed (rc={reason_code})")


def on_message(client, userdata, msg):
    with lock:
        received[msg.topic] = msg.payload.decode(errors="replace")


client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,
                     client_id=f"lightplus_test_{int(time.time())}")
client.on_connect = on_connect
client.on_message = on_message
client.connect(BROKER, PORT, 30)
client.loop_start()

print(f"waiting for device {DEVID} on {BROKER}...")

avail = wait_for(AVAIL_TOPIC)
check(avail == "online", f"availability is online (got {avail!r})")

for topic in DISCOVERY:
    payload = wait_for(topic, 10)
    if payload is None:
        check(False, f"discovery {topic}")
        continue
    try:
        doc = json.loads(payload)
        ok = "unique_id" in doc and "state_topic" in doc and "device" in doc
    except json.JSONDecodeError:
        ok = False
    check(ok, f"discovery {topic.split('/')[1]}/{topic.split('/')[2]}")

state = wait_for(STATE_TOPIC, 10)
try:
    sd = json.loads(state)
    check("brightness" in sd and "state" in sd, "HA state payload has state+brightness")
except Exception:
    check(False, "HA state payload parses")

print("sending commands...")
client.publish(SET_TOPIC, json.dumps({"state": "ON", "brightness": 128}))
time.sleep(4)
sd = json.loads(wait_for(STATE_TOPIC, 5))
check(sd.get("state") == "ON" and sd.get("brightness") == 128,
      f"ON + brightness applied (got {sd.get('state')}/{sd.get('brightness')})")

client.publish(SET_TOPIC, json.dumps({"color_temp": 370}))
time.sleep(4)
sd = json.loads(wait_for(STATE_TOPIC, 5))
check(2650 <= sd.get("cct", 0) <= 2750, f"color_temp mireds applied (cct={sd.get('cct')})")

client.publish(SET_TOPIC, json.dumps({"state": "OFF"}))
time.sleep(4)
sd = json.loads(wait_for(STATE_TOPIC, 5))
check(sd.get("state") == "OFF", f"OFF applied (got {sd.get('state')})")

client.publish(SET_TOPIC, json.dumps({"cmd": "override", "mode": "auto"}))
time.sleep(2)

print("cleaning up retained test messages...")
for topic in DISCOVERY + [STATE_TOPIC, f"{PREFIX}/state", AVAIL_TOPIC]:
    client.publish(topic, "", retain=True)
time.sleep(1)

client.loop_stop()
client.disconnect()

if fails:
    print(f"\n{len(fails)} FAILURES")
    sys.exit(1)
print("\nall MQTT checks passed")
