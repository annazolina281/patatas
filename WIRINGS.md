# Wiring Verification

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

Go ahead and use two laptop USB ports for burn-in — it's clean, safe, and the overcurrent protection on a laptop is better than most cheap wall adapters anyway.
