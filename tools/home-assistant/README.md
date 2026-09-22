# Home Assistant + Mosquitto (Docker)

Runs Home Assistant and an MQTT broker on any machine with Docker. The lamp
connects to the broker and Home Assistant discovers it automatically.

## Requirements

- Docker Desktop (Windows/macOS) or Docker Engine (Linux)
- ~2 GB disk, ~500 MB RAM
- The lamp and this machine on the same network

## Start

```bash
cd tools/home-assistant
docker compose up -d
```

First start takes a few minutes (image download + HA initialisation).

## Configure Home Assistant

1. Open http://localhost:8123 and create your account
2. **Settings → Devices & Services → Add Integration → MQTT**
   - Broker: `mosquitto`  (the compose service name; not localhost)
   - Port: `1883`
   - Username/password: leave empty (anonymous broker — see security below)
3. The **LightPlus** device appears with: light, occupancy, light level, target
   distance and energy entities

## Configure the lamp

Dashboard → **Settings → MQTT (Home Assistant)**:

- Enable: on
- Broker host: this machine's LAN IP (e.g. `192.168.1.20`)
- Port: `1883`
- Save

The device id is shown in the dashboard state broadcast (`id`, e.g. `lpb9c9fc`);
topics are `lightplus/<id>/...`.

## Stop / logs

```bash
docker compose logs -f homeassistant
docker compose down
```

## Security

`allow_anonymous true` is fine for a trusted home LAN or a demo. For anything
more exposed:

1. Generate a password file inside the broker container:

```bash
docker exec lightplus-mosquitto mosquitto_passwd -b -c /mosquitto/data/passwd lightplus <password>
```

2. Edit `mosquitto.conf`: remove `allow_anonymous true`, add
   `password_file /mosquitto/data/passwd`, then `docker compose restart mosquitto`
3. Use those credentials in both the Home Assistant MQTT integration and the
   lamp's MQTT settings.

## Remote access

Home Assistant is only reachable on your LAN by default. Options for remote:
**Home Assistant Cloud** (subscription), Tailscale/WireGuard VPN, or an HTTPS
reverse proxy. Keep the broker itself local.
