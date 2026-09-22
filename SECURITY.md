# Security Policy

## Supported versions

| Version | Supported |
|---|---|
| 1.2.x | yes |
| < 1.2 | no |

## Reporting a vulnerability

Open a private security advisory on GitHub or email the maintainer. Please do
not disclose publicly before a fix is available.

## Threat model

LightPlus is a **local-network device**. It listens on:

- HTTP/WebSocket on port 80 (dashboard + control API)
- espota OTA on port 3232 (password-protected)
- optional setup AP `LightPlus-Setup` (open AP, only while unconfigured or after
  repeated WiFi failures)

Known properties:

- Control commands are unauthenticated unless `WS_TOKEN` is set. Anyone on the
  same network can control the lamp. Set `WS_TOKEN` and/or isolate IoT devices
  on a separate VLAN for stronger protection.
- The setup AP is open by design (initial provisioning). It is disabled once
  the device connects to a saved network. If WiFi fails repeatedly, the AP
  restarts automatically — a physical attacker in radio range could connect and
  change WiFi credentials.
- OTA firmware uploads require `OTA_PASSWORD`.
- No cloud connection, no telemetry, no accounts.

## Hardening checklist for deployments

1. Set a strong `OTA_PASSWORD` and `WS_TOKEN` in `wifi_config.h`.
2. Keep the device on a trusted or isolated network segment.
3. Update firmware only from trusted sources (GitHub releases).
