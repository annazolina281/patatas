# Wiring Verification & Connections

## Overview

Patatas uses four main subsystems: power, sensors (I2C), SPI display, and GPIO peripherals. Each must be wired correctly for proper operation.

---

## Power Setup — ✅ Safe with caveats

**ESP32S3 SuperMini via USB-C from laptop** — perfectly fine. The SuperMini draws well under 500mA during normal Wi-Fi operation.

**MQ-3 + MQ-135 via separate USB (wall or laptop)** — this is where we need to do some math.

| Sensor    | Heater voltage | Heater current (typ.) | Heater power |
| --------- | -------------- | --------------------- | ------------ |
| MQ-3      | 5V             | ~150mA                | ~0.75W       |
| MQ-135    | 5V             | ~150mA                | ~0.75W       |
| **Total** |                | **~300mA**            | **~1.5W**    |

300mA combined is well within a USB port's 500mA limit, so **powering both MQ sensors from a single laptop USB port is totally fine** — and honestly safer than the wall adapter, since laptop USB ports have built-in overcurrent protection. A 5V 3.5A wall adapter is overkill for 300mA, and while it won't damage anything, the laptop USB route is the simpler and safer call for burn-in debugging.

---

## Voltage Divider — ✅ Correct, but let's double-check the math

Your MQ sensors output 0–5V from their AOUT pin. The ADS1115 in default mode tolerates up to VDD + 0.3V on its inputs — and since you're powering the ADS1115 from the ESP32's 3.3V rail, that means **max safe input ≈ 3.6V**.

Your divider: R1 = 1kΩ (top), R2 = 2kΩ (bottom)

```
V_out = V_in × R2 / (R1 + R2)
      = 5V × 2000 / (1000 + 2000)
      = 5V × 0.667
      = 3.33V
```

3.33V < 3.6V — you are just barely within the safe window. ✅ It works, but there's almost no margin. If your USB supply runs slightly high (5.1–5.2V is common), you could briefly exceed the ADS1115's input limit. A slightly safer alternative would be **R1 = 1kΩ, R2 = 1.5kΩ**, giving you ~3.0V at 5V input, with more headroom.

---

## Common Ground — ✅ Essential and correct

Tying all grounds together (laptop USB GND, MQ sensor USB GND, ESP32, ADS1115) is the right call. Without a common ground, your ADS1115 readings would be meaningless or erratic. Just make sure the GND wire from the MQ sensor's USB cable is actually connected to your breadboard ground rail.

---

## ADS1115 Power — one thing to confirm

You mentioned the ADS1115 is connected to the ESP32S3. Confirm it's on **3.3V**, not accidentally on a 5V rail. The ADS1115 is a 3.3V device (VDD range: 2.0–5.5V, so 5V technically works, but then your input voltage ceiling rises to 5.3V and your divider math changes). Keeping everything at 3.3V is cleaner and consistent.

---

## Summary

| Item                                   | Verdict                                    |
| -------------------------------------- | ------------------------------------------ |
| ESP32S3 from laptop USB-C              | ✅ Safe                                    |
| MQ sensors from second laptop USB port | ✅ Safe, preferred over wall               |
| MQ sensors from wall 5V 3.5A           | ✅ Also safe, just unnecessary headroom    |
| Voltage divider 1kΩ / 2kΩ              | ✅ Works, but tight — consider 1kΩ / 1.5kΩ |
| Common ground across all USB sources   | ✅ Required, correct                       |
| ADS1115 on 3.3V rail                   | ✅ Confirm this is the case                |
| ST7789 display on 3.3V SPI             | ✅ Safe, correct                           |
| Button on GPIO2 with internal pull-up  | ✅ Safe, debounced                         |
| DHT22 on GPIO10 (single-wire)          | ✅ Safe, standard protocol                 |
| WS2812B LED on GPIO48                  | ✅ Safe, FastLED managed                   |

Go ahead and use two laptop USB ports for burn-in — it's clean, safe, and the overcurrent protection on a laptop is better than most cheap wall adapters anyway.

---

## ST7789 Display Wiring — ✅ SPI Interface

The 320×240 ST7789 display connects via **SPI** (faster than parallel) and a few GPIO control pins:

| Signal | ESP32S3 Pin | Purpose |
| ------ | ----------- | ------- |
| **CS** | GPIO7 | Chip Select (active LOW) |
| **DC** | GPIO6 | Data/Command (HIGH=data, LOW=command) |
| **RST** | GPIO5 | Reset (active LOW, pull to GND briefly) |
| **BL** | GPIO4 | Backlight (HIGH=on, use PWM for brightness) |
| **MOSI** | GPIO35 | SPI data output (Master Out, Slave In) |
| **MISO** | GPIO37 | SPI data input (Master In, Slave Out) |
| **CLK** | GPIO36 | SPI clock |
| **GND** | GND | Ground (common with ADS1115, sensors) |
| **VDD** | 3.3V | Power (3.3V only, NOT 5V) |

### Guidance

- **3.3V only**: ST7789 is 3.3V logic. Do NOT connect to 5V rail.
- **SPI pins**: ESP32S3 has multiple SPI buses. Pins 35, 36, 37 use SPI3 (verified in `platformio.ini`).
- **CS, DC, RST**: Standard GPIO, no special requirements.
- **Backlight**: Currently set to always ON (GPIO4 → HIGH). Can add PWM for brightness control.
- **No pull-ups needed** on SPI lines; the display module typically includes them or uses pull values suitable for 3.3V.

### Typical Module Pinout

Most ST7789 breakout boards label pins as:
```
GND | VCC (3.3V)
CS  | DC
RST | BL
MOSI | MISO | CLK
```

Wire each in order to the ESP32S3 pins listed above.

---

## Button (Calibration) Wiring — ✅ Simple GPIO

The push button on **GPIO2** triggers baseline calibration when held for 3 seconds:

| Pin | Connection |
| --- | ---------- |
| **GPIO2** | One side of button |
| **GND** | Other side of button |

### Connection Diagram

```
      +3.3V
        |
      (internal pull-up on GPIO2)
        |
       [ Button ] ← press to connect GPIO2 to GND
        |
       GND
```

The button uses the **internal pull-up resistor** on GPIO2, so no external resistor is needed. When the button is pressed, GPIO2 is pulled to GND (read LOW).

### Debouncing

The firmware includes debouncing in the button ISR to avoid false triggers from switch bounce. Typical mechanical buttons need ~20–50ms debounce time, which is handled in the firmware.

### Behavior

- **Press and hold 3 seconds** → LED flashes green, baseline saved
- **Serial output**: `[Button] Baseline saved! MQ3=X.X, MQ135=Y.Y...`
- **Display updates** to show new baseline values

---

## I2C Bus (DHT22 & ADS1115) — ✅ Verified

| Device | SDA (GPIO8) | SCL (GPIO9) | Address |
| ------ | ----------- | ----------- | ------- |
| **ADS1115** | ✅ | ✅ | 0x48 (A0 to GND) |
| **DHT22** | N/A (signal pin) | N/A | GPIO10 |

- I2C runs at **100 kHz** (standard speed)
- Both ADS1115 and any other I2C devices share the same SDA/SCL bus
- DHT22 uses a **dedicated GPIO10** (single-wire digital), not I2C

---

## Complete Wiring Checklist

### Power Rails
- [ ] 5V USB → MQ-3 VCC, MQ-135 VCC
- [ ] 3.3V (ESP32) → ADS1115 VDD, ST7789 VDD, DHT22 VCC
- [ ] GND → All grounds common (MQ USB GND, ESP32, ADS1115, DHT22, ST7789, button)

### I2C (GPIO8, GPIO9)
- [ ] ADS1115 SDA → GPIO8
- [ ] ADS1115 SCL → GPIO9
- [ ] MQ-3 AOUT → ADS1115 A0 (through 1kΩ/2kΩ divider)
- [ ] MQ-135 AOUT → ADS1115 A1 (through 1kΩ/2kΩ divider)

### DHT22 (GPIO10)
- [ ] DHT22 data pin → GPIO10
- [ ] DHT22 VCC → 3.3V
- [ ] DHT22 GND → GND

### SPI Display (GPIO35, 36, 37, 4, 5, 6, 7)
- [ ] ST7789 VDD → 3.3V
- [ ] ST7789 GND → GND
- [ ] ST7789 SCL (CLK) → GPIO36
- [ ] ST7789 SDA (MOSI) → GPIO35
- [ ] ST7789 MISO → GPIO37
- [ ] ST7789 CS → GPIO7
- [ ] ST7789 DC → GPIO6
- [ ] ST7789 RST → GPIO5
- [ ] ST7789 BL → GPIO4

### Button (GPIO2)
- [ ] Button pin 1 → GPIO2
- [ ] Button pin 2 → GND

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
| ------- | ------------ | --- |
| **Display shows garbage** | Wrong SPI pins or speed | Verify GPIO35/36/37 in code; try lowering SPI speed |
| **Display is blank/dark** | Backlight off or RST held LOW | Check GPIO5 (RST) is HIGH; GPIO4 (BL) is HIGH |
| **Button never triggers** | Reverse polarity or floating GPIO | Ensure GPIO2 connects to GND when pressed; check internal pull-up enabled |
| **ADS1115 reads 0xFFFF** | No I2C communication | Verify GPIO8/GPIO9; check ADS1115 address (0x48) |
| **DHT22 reads 0°C, 0% RH** | No signal or slow read | Ensure GPIO10 is free (not used by SPI or other); allow 2s warm-up |
| **LED doesn't light up** | GPIO48 not connected or FastLED not initialized | Check GPIO48 wiring; verify LED data line polarity |
