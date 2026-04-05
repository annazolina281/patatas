# Patatas: ESP32S3 Potato Rot Monitoring

Patatas is an ESP32S3 + ADS1115 sensor project for monitoring gas trends that can indicate potato spoilage.

Current firmware focuses on local monitoring through Serial output and includes:

- MQ-3 gas sensor monitoring (mapped as alcohol ppm estimate)
- MQ-135 gas sensor monitoring (mapped as air quality ppm estimate)
- DHT22 temperature and humidity reading
- FreeRTOS task-based architecture
- One-shot calibration helper for averaging sensor voltages and suggesting updated R0 constants

## Hardware

- ESP32-S3 (DevKit/SuperMini)
- ADS1115 ADC module
- MQ-3 gas sensor
- MQ-135 gas sensor
- DHT22 sensor
- WS2812B single LED (status)
- Voltage dividers for each MQ analog output

## Pin Mapping

From the current source code:

- DHT22 data pin: GPIO10
- I2C SDA (ADS1115): GPIO8
- I2C SCL (ADS1115): GPIO9
- WS2812B LED data: GPIO48
- ADS1115 channel A0: MQ-3 analog (through divider)
- ADS1115 channel A1: MQ-135 analog (through divider)

## Electrical Notes

- MQ sensor modules are typically powered from 5V.
- ADS1115 inputs in this project are treated as 3.3V-domain, so each MQ analog output must be reduced through a divider.
- The project uses divider compensation in code with a ratio of 1.5.
- Keep all grounds common between ESP32, ADS1115, and sensor supplies.

See WIRINGS.md for deeper wiring discussion and safety notes.

## Software Stack

- PlatformIO (Arduino framework)
- ESP32-S3 target
- Libraries (from platformio.ini):
  - Adafruit DHT sensor library
  - Adafruit ADS1X15
  - FastLED
  - Blynk (currently not used by firmware logic)

## Project Structure

- src/main.cpp: main firmware logic
- include/secrets.h: local credentials/tokens (do not commit real secrets)
- platformio.ini: board, framework, and dependency config
- WIRINGS.md: wiring and power notes

## Build and Upload (PlatformIO)

1. Open the project in VS Code with PlatformIO extension.
2. Connect the ESP32S3 board.
3. Build the firmware.
4. Upload firmware.
5. Open Serial Monitor at 115200 baud.

CLI equivalent:

- Build: pio run
- Upload: pio run -t upload
- Monitor: pio device monitor -b 115200

## Runtime Behavior

The firmware creates two RTOS tasks:

- dht22Task: reads temperature/humidity every 5 seconds
- adsTask: reads ADS1115 channels every 5 seconds, prints compensated voltages and computed ppm values

Example Serial line format:
V0: <value>V | V1: <value>V |PPM = MQ-3 : <value> | MQ 135:<value>

## Calibration Workflow

The code includes a one-shot function:

- runCalibration(sampleCount = 50, sampleDelayMs = 100, loadResistor = 10.0f)

What it does:

- Takes 50 samples (default) from ADS channel 0 and 1
- Applies divider compensation
- Returns averaged V0 and V1
- Prints suggested R0 values using:
  - R0 = RL x (5.0 - V) / V

How to use:

1. Place sensors in clean, stable air.
2. In src/main.cpp setup(), uncomment:
   - calibrationData cal = runCalibration();
3. Upload and open Serial Monitor.
4. Record Average V0, Average V1, Suggested MQ3_RO, Suggested MQ135_RO.
5. Update constants in src/main.cpp:
   - MQ3_RO
   - MQ135_RO
6. Re-comment the calibration line after finishing.

## Tuning Guidance

- Use this system primarily for trend detection rather than absolute gas identity.
- Recalibrate when changing environment, sensor placement, or power conditions.
- Let MQ sensors warm up and stabilize before collecting calibration samples.

## Security and Secrets

- Do not commit real Wi-Fi passwords or cloud tokens.
- Keep include/secrets.h local or replace values with placeholders before sharing.

## Next Improvements

- Add Serial command trigger for calibration (no code uncommenting needed)
- Add rolling average filter for smoother ppm display
- Add threshold-based alert logic for spoilage detection
- Optionally remove unused Blynk dependency from platformio.ini if cloud is no longer needed
