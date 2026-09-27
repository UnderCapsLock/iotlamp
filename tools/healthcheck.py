#!/usr/bin/env python3
"""LightPlus system health check.

Checks: lamp discovery, dashboard, WebSocket state, radar stream, MQTT broker,
lamp MQTT connection, retained broker data, Docker containers, Home Assistant.

Usage: python tools/healthcheck.py [--ip 192.168.0.7] [--mac-suffix b9-c9-fc]
Requires: pip install paho-mqtt (only for the broker checks)
"""

import argparse
import base64
import json
import os
import re
import socket
import subprocess
import sys
import time
import urllib.request
from concurrent.futures import ThreadPoolExecutor

RESULTS = []


def report(name, ok, detail=""):
    mark = "PASS" if ok else "FAIL"
    print(f"[{mark}] {name}" + (f" -- {detail}" if detail else ""))
    RESULTS.append((name, ok))


def note(name, value):
    print(f"       {name}: {value}")


def local_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    finally:
        s.close()


def arp_find(mac_suffix):
    suffix = mac_suffix.lower().replace(":", "-")
    try:
        out = subprocess.run(["arp", "-a"], capture_output=True, text=True, timeout=10).stdout
    except Exception:
        return None
    for m in re.finditer(r"(\d+\.\d+\.\d+\.\d+)\s+([0-9a-fA-F-]{17})", out):
        ip, mac = m.group(1), m.group(2).lower()
        if mac.endswith(suffix):
            return ip
    return None


def discover(mac_suffix, ip_hint=None):
    if ip_hint:
        return ip_hint
    try:
        return socket.gethostbyname("lightplus.local")
    except Exception:
        pass
    found = arp_find(mac_suffix)
    if found:
        return found
    ip = local_ip()
    subnet = ip.rsplit(".", 1)[0]

    def ping(i):
        subprocess.run(["ping", "-n", "1", "-w", "150", f"{subnet}.{i}"],
                       capture_output=True, timeout=5)

    with ThreadPoolExecutor(max_workers=64) as ex:
        list(ex.map(ping, range(1, 255)))
    return arp_find(mac_suffix)


def read_ws_state(ip, timeout=6):
    s = socket.create_connection((ip, 80), timeout=timeout)
    key = base64.b64encode(os.urandom(16)).decode()
    req = (f"GET /ws HTTP/1.1\r\nHost: {ip}\r\nUpgrade: websocket\r\n"
           f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
           f"Sec-WebSocket-Version: 13\r\n\r\n")
    s.sendall(req.encode())
    buf = b""
    while b"\r\n\r\n" not in buf:
        chunk = s.recv(1024)
        if not chunk:
            raise RuntimeError("connection closed during handshake")
        buf += chunk
    head, rest = buf.split(b"\r\n\r\n", 1)
    if b"101" not in head.split(b"\r\n")[0]:
        raise RuntimeError("websocket upgrade rejected")
    buf = rest
    end = time.time() + timeout
    while time.time() < end:
        s.settimeout(max(0.5, end - time.time()))
        frame = parse_frame(buf)
        while frame is None:
            chunk = s.recv(2048)
            if not chunk:
                raise RuntimeError("connection closed")
            buf += chunk
            frame = parse_frame(buf)
        (opcode, payload), buf = frame
        if opcode == 1:
            try:
                msg = json.loads(payload.decode("utf-8", "replace"))
                if "dark" in msg:
                    return msg
            except Exception:
                pass
    raise RuntimeError("no state message received")


def parse_frame(buf):
    if len(buf) < 2:
        return None
    opcode = buf[0] & 0x0F
    length = buf[1] & 0x7F
    idx = 2
    if length == 126:
        if len(buf) < 4:
            return None
        length = int.from_bytes(buf[2:4], "big")
        idx = 4
    elif length == 127:
        if len(buf) < 10:
            return None
        length = int.from_bytes(buf[2:10], "big")
        idx = 10
    if len(buf) < idx + length:
        return None
    return (opcode, buf[idx:idx + length]), buf[idx + length:]


def http_ok(url, timeout=5):
    try:
        with urllib.request.urlopen(url, timeout=timeout) as r:
            return r.status == 200
    except Exception:
        return False


def broker_retained():
    try:
        import paho.mqtt.client as mqtt
    except ImportError:
        return None, None
    got = {}

    def on_message(c, u, m):
        got[m.topic] = m.payload.decode(errors="replace")

    c = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="healthcheck_py")
    c.on_message = on_message
    try:
        c.connect("127.0.0.1", 1883, 10)
    except Exception:
        return None, None
    c.subscribe("lightplus/+/availability")
    c.subscribe("lightplus/+/state")
    c.loop_start()
    time.sleep(3)
    c.loop_stop()
    c.disconnect()
    avail = next((v for k, v in got.items() if k.endswith("/availability")), None)
    state = next((v for k, v in got.items() if k.endswith("/state") and "ha/" not in k), None)
    return avail, state


def docker_status():
    docker = os.path.join(os.environ.get("LOCALAPPDATA", ""),
                          "Programs", "DockerDesktop", "resources", "bin", "docker.exe")
    if not os.path.exists(docker):
        docker = "docker"
    try:
        out = subprocess.run([docker, "ps", "--format", "{{.Names}}={{.Status}}"],
                             capture_output=True, text=True, timeout=20).stdout
        return out.strip().splitlines()
    except Exception:
        return []


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ip", help="lamp IP (skips discovery)")
    ap.add_argument("--mac-suffix", default="b9-c9-fc", help="lamp MAC suffix for discovery")
    args = ap.parse_args()

    print("LightPlus Health Check")
    print("=" * 50)
    lip = local_ip()
    note("Laptop IP", lip)

    ip = discover(args.mac_suffix, args.ip)
    report("Lamp discovered", bool(ip), ip or "not found")
    if not ip:
        finish()
        return

    report("Dashboard reachable", http_ok(f"http://{ip}/"), f"http://{ip}/")

    state = None
    try:
        state = read_ws_state(ip)
        report("WebSocket state", True, f"fw {state.get('fw')}, uptime {state.get('uptime_s')}s")
    except Exception as e:
        report("WebSocket state", False, str(e))

    if state:
        time.sleep(3)
        try:
            state2 = read_ws_state(ip)
            delta = state2.get("radar_frames", 0) - state.get("radar_frames", 0)
            report("Radar streaming", delta > 0 or state2.get("radar_frames", 0) > 0,
                   f"frames +{delta} in 3s (total {state2.get('radar_frames')})")
        except Exception as e:
            report("Radar streaming", False, str(e))
        note("Presence", state.get("presence"))
        note("Room", "dark" if state.get("dark") else "bright")
        report("Lamp MQTT connected", bool(state.get("mqtt_on")),
               f"broker {state.get('mqtt_host')}:{state.get('mqtt_port')}")
        if state.get("mqtt_host") and state["mqtt_host"] != lip:
            note("WARNING", f"lamp targets {state['mqtt_host']} but this laptop is {lip} — "
                            f"run set_mqtt with host {lip}")

    report("MQTT broker port 1883", socket_test("127.0.0.1", 1883))
    avail, retained = broker_retained()
    if avail is not None:
        report("Broker availability", avail == "online", f"lamp says '{avail}'")
        report("Broker retained state", retained is not None,
               "receiving" if retained else "no retained state topic")
    else:
        report("Broker checks", False, "paho-mqtt missing or broker unreachable")

    containers = docker_status()
    ha = any(c.startswith("lightplus-homeassistant=Up") for c in containers)
    mq = any(c.startswith("lightplus-mosquitto=Up") for c in containers)
    report("Docker containers", ha and mq,
           ", ".join(containers) if containers else "docker not running")

    report("Home Assistant UI", http_ok("http://localhost:8123"), "http://localhost:8123")

    finish()


def socket_test(host, port):
    try:
        with socket.create_connection((host, port), timeout=3):
            return True
    except Exception:
        return False


def finish():
    fails = [n for n, ok in RESULTS if not ok]
    print("-" * 50)
    if fails:
        print(f"RESULT: {len(fails)} problem(s): " + "; ".join(fails))
        sys.exit(1)
    print("RESULT: all checks passed")
    sys.exit(0)


if __name__ == "__main__":
    main()
