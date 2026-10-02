# LightPlus — User Guide

A presence-aware lamp that lights up only when the room is dark **and** someone
is there — even if that person is sitting perfectly still. No cloud, no account,
no subscription.

## What's in the box

- LightPlus lamp (assembled and tested)
- 5V power supply (or USB cable for a power bank)
- This guide

## 1. Mount it

Ceiling or high wall, ideally above the main activity area. The presence sensor
and light sensor are built in, so there is nothing else to wire or aim
precisely — just avoid mounting directly in front of metal surfaces.

## 2. Power it on

Plug it in. The LED ring briefly glows as it starts up (~30 seconds). If it has
been set up before, it simply reconnects and starts working.

## 3. Connect it to your Wi-Fi (one-time, ~2 minutes)

The first time, the lamp does not know your network yet:

1. On your phone, open Wi-Fi settings and join **`LightPlus-Setup`** (open, no password)
2. A setup page usually opens by itself — otherwise open a browser and go to **192.168.4.1**
3. Enter your Wi-Fi name (SSID) and password, then **Save & restart**
4. The lamp reboots, joins your network, and remembers it permanently

> **Using a phone hotspot?** Some phones share only 5 GHz by default. Turn on
> **"Maximize Compatibility"** (iPhone: Settings → Personal Hotspot) or set the
> hotspot band to 2.4 GHz (Android). The lamp can remember up to 3 networks —
> home, office, and hotspot all at once.

## 4. Open the dashboard

From any phone or laptop on the same Wi-Fi:

- Open a browser and go to **`http://lightplus.local`** — or find the lamp in
  your router's connected-devices list and open that address
- The page works like an app: on Android Chrome you can use **Install app** to
  put it on your home screen, and a native Android app is included with the system

The dashboard shows live status, brightness and color controls, scenes, a live
radar view, the schedule, energy use and presence tuning — all served by the
lamp itself.

## 5. Optional: Home Assistant

If you run Home Assistant with an MQTT broker (e.g. the Mosquitto add-on):

1. Dashboard → Settings → **MQTT (Home Assistant)** → enable, enter the broker
   address (and credentials if set) → Save
2. The lamp announces itself automatically and appears as a device with:
   light, occupancy, room light, distance and energy entities — plus an
   **Auto mode** switch to return to automatic behavior

## 6. Everyday use

You mostly do nothing:

- **Lights up** when you enter a dark room, stays on while you're there
- **Turns off** shortly after you leave (adjustable hold time)
- **Dims warmly** toward bedtime, **brightens gently** at your wake time
- **Night-light mode** (optional) keeps a faint glow instead of full darkness
- **Manual control** from the dashboard any time: On/Off/Auto, brightness,
  color, scenes, sleep timer

## Troubleshooting

| Symptom | What to do |
|---|---|
| Lamp doesn't come on in a dark room | Check the radar in Settings → Presence tuning; someone must be within the detection window |
| Changed Wi-Fi and can't reach it | It creates `LightPlus-Setup` after ~1 minute — join it and enter the new network, or press **Change WiFi** in Settings first |
| Dashboard address not opening | Same Wi-Fi? Find the lamp's address in the router list, or try `lightplus.local` |
| Presence seems too eager / not eager enough | Settings → Presence tuning: sensitivity level, minimum/maximum distance window |
| Light stays on for a while after leaving | That's the presence hold — shorten it in Settings → "When no one is there" |

## Care

- Use the provided 5V supply (2A recommended); a phone power bank also works
- Don't cover the sensor's front face or spray liquids into the housing
- Updates and settings survive power cuts

## For the owner / installer

Detailed diagnostics tools live in the project repository:

- `check.bat` — full system check (lamp, radar, MQTT, Home Assistant) with
  automatic fixes; run it any time something feels off
- `dashboard.bat` — finds the lamp's current address and opens the dashboard
- See `README.md` for firmware, wiring, and integration documentation
