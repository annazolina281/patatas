# Patatas: ESP32S3 Potato Rot Detector

Patatas is a complete potato rot detection system using an ESP32S3, gas sensors (MQ-3, MQ-135), and environmental sensors (DHT22) with real-time display feedback via ST7789 touch display.

## Features

- **Real-time Rot Detection**: 4-state verdict system (FRESH → MONITOR → SUSPECT → ROTTEN)
- **Intelligent Scoring**: Multi-sensor fusion with hysteresis algorithm
- **One-Button Calibration**: 3-second hold on GPIO2 to set baseline from clean potato
- **Live Display**: 320×240 ST7789 LCD showing baseline readings, current measurements, and verdict
- **Color-Coded Status**: WS2812B LED indicates rot status (Green/Yellow/Orange/Red)
- **Persistent Baseline**: NVS storage survives power cycles
- **Dual-Core Processing**: FreeRTOS sensor tasks on Core 0, display updates on Core 1
- **Serial Monitoring**: 10-second debug output to serial monitor (115200 baud)

## Hardware

- **ESP32-S3** (DevKit/SuperMini)
- **ADS1115** ADC module (16-bit, I2C)
- **MQ-3** gas sensor (alcohol/ethanol)
- **MQ-135** gas sensor (CO2/air quality)
- **DHT22** sensor (temperature & humidity)
- **ST7789** display (320×240, SPI)
- **WS2812B** addressable LED (status indicator)
- **Push Button** (for baseline calibration)
- Voltage dividers for MQ sensors (1kΩ / 2kΩ recommended)

## Pin Mapping

| Component      | Pin    | Purpose              |
| -------------- | ------ | -------------------- |
| DHT22          | GPIO10 | Temperature/Humidity |
| I2C SDA        | GPIO8  | ADS1115 data         |
| I2C SCL        | GPIO9  | ADS1115 clock        |
| WS2812B LED    | GPIO48 | Status indicator     |
| Button         | GPIO2  | Calibration trigger  |
| TFT CS         | GPIO7  | Display chip select  |
| TFT DC         | GPIO6  | Display data/command |
| TFT RST        | GPIO5  | Display reset        |
| TFT BL         | GPIO4  | Display backlight    |
| TFT SPI MOSI   | GPIO35 | SPI data out         |
| TFT SPI MISO   | GPIO37 | SPI data in          |
| TFT SPI CLK    | GPIO36 | SPI clock            |
| ADS1115 A0     | —      | MQ-3 analog input    |
| ADS1115 A1     | —      | MQ-135 analog input  |

## Electrical Notes

- MQ sensor modules are typically powered from 5V.
- ADS1115 inputs in this project are treated as 3.3V-domain, so each MQ analog output must be reduced through a divider.
- The project uses divider compensation in code with a ratio of 1.5.
- Keep all grounds common between ESP32, ADS1115, and sensor supplies.

See WIRINGS.md for deeper wiring discussion and safety notes.

## Software Stack

- **PlatformIO** (Arduino framework)
- **ESP32-S3** target with FreeRTOS
- **Key Libraries**:
  - Adafruit DHT sensor library (DHT22)
  - Adafruit ADS1X15 (ADC for MQ sensors)
  - Adafruit ST7789 (display driver)
  - FastLED (WS2812B addressable LED)
  - Blynk (optional, for remote monitoring)

## Project Structure

```
patatas/
├── src/
│   └── main.cpp              # Core firmware: sensors, detection, display
├── include/
│   ├── potato_data.h         # Data structures (SensorReadings, VerdictState)
│   ├── pinouts.h             # Pin definitions
│   ├── secrets.h             # Wi-Fi/Blynk credentials (local only)
│   └── screen.h              # ST7789 display layout and updates
├── lib/
│   └── README
├── test/
│   └── README
├── platformio.ini            # Board, framework, libraries
├── README.md                 # This file
└── WIRINGS.md                # Electrical connections and power notes
```

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

## Runtime Behavior & Detection Logic

### Startup
1. Display initializes with black background and white text
2. Shows "UNCALIBRATED" until baseline is set
3. Blue LED indicates waiting for calibration

### Baseline Calibration
- **Hold button (GPIO2) for 3 seconds** on a fresh potato
- Captures 10-sample average of: MQ-3, MQ-135, temperature, humidity
- Saves to NVS (survives power cycles)
- Green LED blinks to confirm

### Active Monitoring
The system continuously:
1. Reads all sensors every 500ms
2. Maintains rolling 10-point average buffer
3. Calculates deltas from baseline
4. Scores rot likelihood:

#### Rot Scoring Algorithm
| Factor | Threshold | Points |
| ------ | ---------- | ------ |
| **MQ-3 alcohol** | > 150 ppm delta | +3 |
|  | > 80 ppm delta | +2 |
|  | > 30 ppm delta | +1 |
| **MQ-135 air quality** | > 60 ppm delta | +2 |
|  | > 20 ppm delta | +1 |
| **Humidity** | > 85% | +1 |
| **Temperature** | > 25°C | +1 |

#### Verdict Thresholds
| Score | Status | LED | Meaning |
| ----- | ------ | --- | ------- |
| 0–1 | **FRESH** | 🟢 Green | Healthy, safe to eat |
| 2–3 | **MONITOR** | 🟡 Yellow | Early decay signs, watch closely |
| 4–5 | **SUSPECT** | 🟠 Orange | Advanced decay, use within hours |
| 6+ | **ROTTEN** | 🔴 Red | Spoiled, do not eat |

#### Hysteresis Algorithm
- **Upgrading verdict** (worsening): requires **3 consecutive readings**
- **Downgrading verdict** (improving): requires **5 consecutive readings**
- Prevents false alarms from sensor noise

### Display Updates
- Every 5 seconds: displays baseline values, current readings, and verdict
- Baseline section: MQ-3, MQ-135, Temp, Humidity from calibration
- Previous section: Current PPM readings and environmental data
- Status line: Current verdict (FRESH/MONITOR/SUSPECT/ROTTEN/UNCAL)

### Serial Monitor Output
Every 10 seconds, prints:
```
[Sensor] Temp=22.5 C, Humidity=65.0 %, MQ3=45.2 ppm, MQ135=12.8 ppm
```

## Usage Instructions

### Initial Setup
1. **Connect all sensors** per WIRINGS.md
2. **Upload firmware** (see Build and Upload below)
3. **Open Serial Monitor** at 115200 baud

### First Time Use
1. Place detector near a **fresh potato** in normal room conditions
2. **Hold button for 3 seconds** until LED flashes green
3. Serial output: `[Button] Baseline saved! MQ3=X.X, MQ135=Y.Y, Temp=Z.Z C`
4. Display now shows baseline values

### Testing a Potato
1. Place suspect potato about 5cm from MQ sensors
2. **Wait 5 seconds** for first reading
3. **Watch for verdict change**:
   - LED stays green → FRESH ✅
   - LED turns yellow → MONITOR ⚠️ (decay starting)
   - LED turns orange → SUSPECT ⚠️ (advanced decay)
   - LED turns red → ROTTEN ❌ (spoiled)

### Re-calibrating
- Hold button for 3 seconds with a **new reference potato**
- Old baseline is replaced in NVS

### Factory Reset
- Use Arduino IDE or PlatformIO to erase NVS partition (optional)
- Or update the baseline with any potato
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
