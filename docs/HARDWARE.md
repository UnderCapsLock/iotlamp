# Hardware

## Overview

```
        ┌─────────────────────────────┐
        │           ESP32             │
        │                             │
  5V ───┤ 5V                     GP5 ├───► LED ring data (WS2812)
 GND ───┤ GND                         │
        │      GPIO16 ◄─── TX ────────┤   LD2410C radar (UART2, 256000 baud)
        │      GPIO17 ──── RX ────────┤
        │      GPIO4  ◄──── OUT ──────┤   (presence level, optional)
        │      GPIO34 ◄─── divider ───┤   LDR (analog)
        └─────────────────────────────┘
```

| ESP32 pin | Connected to | Notes |
|---|---|---|
| GPIO 5 | LED ring data | WS2812 (12 LEDs) |
| GPIO 16 | LD2410C TX | UART2 RX, 256000 baud |
| GPIO 17 | LD2410C RX | UART2 TX |
| GPIO 4 | LD2410C OUT | optional presence level |
| GPIO 34 | LDR divider | ADC1, 12-bit |
| 5V / GND | LED ring + radar | external 5V recommended for the ring |

## Power (important)

The LED ring at full white draws roughly 0.6–0.7 A. If the ring and radar
share a weak supply (e.g. a laptop USB port limited to 500 mA), the radar
browns out and stops streaming while the LEDs are on. Verified fix: give the
ring a solid 5 V supply (or power the board from a 2 A source). The firmware
also auto-restarts the radar if its stream stalls, but it cannot fix an
under-sized supply.

## Upgrades & cost cutting

Current retail BOM ≈ RM50. Options, biggest savings first:

| Change | Effort | Saving / effect |
|---|---|---|
| ESP32-C3 module instead of dev board | easy | −RM8–12, fewer GPIOs (enough) |
| Custom PCB with bare WROOM module + JST connectors | medium | −RM5–8, huge assembly-time saving |
| Dual-white CCT LEDs instead of 12× WS2812 | medium | −RM8, higher CRI, **⅓ the current**, loses RGB |
| Single RGB LED + diffuser | easy | −RM10, accent-light product variant |
| Bulk radar sourcing (100 pcs) | — | LD2410C RM20 → RM12–14 |
| INA219 shunt for true energy metering | easy | +RM5, makes the energy feature honest |
| 74HCT1G125 level shifter for WS2812 data | easy | +RM0.50, reliable 3.3 V data |
| Integrated 5 V AC-DC module (HLK-PM01) | medium | single-cable product |

Realistic target at small volume: **RM25–30 BOM**.

Do **not** replace the LD2410C with a cheap PIR or RCWL-0516: stationary
presence detection is the product's core value and those sensors cannot do it.
