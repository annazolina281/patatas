#include <Arduino.h>
#include <secrets.h>
#include <pinouts.h>
#include <potato_data.h>

// NVS storage
#include <Preferences.h>
Preferences preferences;
#define NVS_NAMESPACE "potato"

// Screen display
#include <screen.h>
#include <Adafruit_ST7789.h>
Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

// libraries and definitions
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#define DHTTYPE DHT22

#include <Wire.h>
#include <Adafruit_ADS1X15.h>
Adafruit_ADS1115 ads;

#include <FastLED.h>
#define LED_PIN 48
#define NUM_LEDS 1
#define BRIGHTNESS 64
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB

// initializations
DHT_Unified dht(DHT22_PIN, DHTTYPE);
CRGB leds[NUM_LEDS];

// structs
struct dhtData {
  float temperature;
  float humidity;
};

struct adsData {
  float alcohol_ppm; // from MQ3 
  float air_quality_ppm; // from MQ135
};

struct calibrationData {
  float avg_v0;
  float avg_v1;
  bool valid;
};

// R0 = RL × (5.0 - V) / V

// PPM computations
const float MQ3_RO = 96.0; // TODO: these values should be changed PER calibration
const float MQ3_A = 0.3934;
const float MQ3_B = -1.504;

const float MQ135_RO = 115.0; // TODO: these values should be changed PER calibration
const float MQ135_A = 102.2;
const float MQ135_B = -2.55;

// ============ POTATO ROT DETECTION GLOBALS ============
BaselineData baseline = {0.0f, 0.0f, 0.0f, 0.0f, 0, false};
SensorReadings current_readings = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, UNCALIBRATED, false, false};

// Buffer for 5-reading rolling average
#define READING_BUFFER_SIZE 5
struct {
  float mq3[READING_BUFFER_SIZE];
  float mq135[READING_BUFFER_SIZE];
  float temp[READING_BUFFER_SIZE];
  float humidity[READING_BUFFER_SIZE];
  uint8_t index;
} reading_buffer = {{0}, {0}, {0}, {0}, 0};

// Button handling
volatile uint32_t button_press_time = 0;
volatile bool button_pressed = false;
#define BASELINE_HOLD_TIME_MS 3000

// Hysteresis tracking
uint32_t consecutive_readings_at_score = 0;
VerdictState last_verdict = UNCALIBRATED;

// RTOS Queues
QueueHandle_t dhtQueue, adsQueue;

const float DIVIDER_RATIO = 1.5; // inverse of 2/(1+2) in kR

// ============ NVS BASELINE FUNCTIONS ============
bool loadBaseline() {
  preferences.begin(NVS_NAMESPACE, true); // Read-only mode
  baseline.is_valid = preferences.getBool("baseline_valid", false);
  if (baseline.is_valid) {
    baseline.mq3_baseline = preferences.getFloat("mq3_base", 0.0f);
    baseline.mq135_baseline = preferences.getFloat("mq135_base", 0.0f);
    baseline.baseline_temp = preferences.getFloat("base_temp", 0.0f);
    baseline.baseline_humidity = preferences.getFloat("base_humidity", 0.0f);
    baseline.baseline_timestamp = preferences.getUInt("base_timestamp", 0);
    Serial.println("[NVS] Baseline loaded from storage");
    return true;
  }
  preferences.end();
  Serial.println("[NVS] No baseline found in storage");
  return false;
}

bool saveBaseline(float mq3, float mq135, float temp, float humidity) {
  preferences.begin(NVS_NAMESPACE, false); // Read-write mode
  baseline.mq3_baseline = mq3;
  baseline.mq135_baseline = mq135;
  baseline.baseline_temp = temp;
  baseline.baseline_humidity = humidity;
  baseline.baseline_timestamp = millis() / 1000; // Simple timestamp
  baseline.is_valid = true;
  
  preferences.putBool("baseline_valid", true);
  preferences.putFloat("mq3_base", mq3);
  preferences.putFloat("mq135_base", mq135);
  preferences.putFloat("base_temp", temp);
  preferences.putFloat("base_humidity", humidity);
  preferences.putUInt("base_timestamp", baseline.baseline_timestamp);
  preferences.end();
  
  Serial.println("[NVS] Baseline saved to storage");
  return true;
}

// ============ BUTTON INTERRUPT HANDLER ============
void IRAM_ATTR buttonISR() {
  if (!button_pressed) {
    button_press_time = millis();
    button_pressed = true;
  }
}

// ============ BUFFER & AVERAGING ============
void addToReadingBuffer(float mq3, float mq135, float temp, float humidity) {
  reading_buffer.mq3[reading_buffer.index] = mq3;
  reading_buffer.mq135[reading_buffer.index] = mq135;
  reading_buffer.temp[reading_buffer.index] = temp;
  reading_buffer.humidity[reading_buffer.index] = humidity;
  reading_buffer.index = (reading_buffer.index + 1) % READING_BUFFER_SIZE;
}

void getAveragedReadings(float &mq3_avg, float &mq135_avg, float &temp_avg, float &humidity_avg) {
  mq3_avg = 0; mq135_avg = 0; temp_avg = 0; humidity_avg = 0;
  for (int i = 0; i < READING_BUFFER_SIZE; i++) {
    mq3_avg += reading_buffer.mq3[i];
    mq135_avg += reading_buffer.mq135[i];
    temp_avg += reading_buffer.temp[i];
    humidity_avg += reading_buffer.humidity[i];
  }
  mq3_avg /= READING_BUFFER_SIZE;
  mq135_avg /= READING_BUFFER_SIZE;
  temp_avg /= READING_BUFFER_SIZE;
  humidity_avg /= READING_BUFFER_SIZE;
}

// ============ ROT SCORING ALGORITHM ============
void calculateVerdictFromScore(int &rot_score, VerdictState &verdict) {
  // Reset score
  rot_score = 0;
  
  // Only score if baseline is set
  if (!baseline.is_valid) {
    verdict = UNCALIBRATED;
    return;
  }
  
  // MQ-3 scoring (primary signal)
  if (current_readings.mq3_delta > 150) {
    rot_score += 3;
  } else if (current_readings.mq3_delta > 80) {
    rot_score += 2;
  } else if (current_readings.mq3_delta > 30) {
    rot_score += 1;
  }
  
  // MQ-135 scoring (confirmation)
  if (current_readings.mq135_delta > 60) {
    rot_score += 2;
  } else if (current_readings.mq135_delta > 20) {
    rot_score += 1;
  }
  
  // DHT22 environmental modifiers
  if (current_readings.humidity > 85.0f) {
    rot_score += 1;
  }
  if (current_readings.temperature > 25.0f) {
    rot_score += 1;
  }
  
  // Determine verdict based on score
  VerdictState new_verdict;
  if (rot_score >= 6) {
    new_verdict = ROTTEN;
  } else if (rot_score >= 4) {
    new_verdict = SUSPECT;
  } else if (rot_score >= 2) {
    new_verdict = MONITOR;
  } else {
    new_verdict = FRESH;
  }
  
  // Apply hysteresis
  if (new_verdict == last_verdict) {
    consecutive_readings_at_score++;
  } else if (new_verdict > last_verdict) {
    // Upgrading verdict: require 3 consecutive readings
    consecutive_readings_at_score++;
    if (consecutive_readings_at_score < 3) {
      new_verdict = last_verdict;
    } else {
      last_verdict = new_verdict;
      consecutive_readings_at_score = 0;
    }
  } else {
    // Downgrading verdict: require 5 consecutive readings
    consecutive_readings_at_score++;
    if (consecutive_readings_at_score < 5) {
      new_verdict = last_verdict;
    } else {
      last_verdict = new_verdict;
      consecutive_readings_at_score = 0;
    }
  }
  
  verdict = new_verdict;
}

// ============ DISPLAY UPDATE FUNCTIONS ============
// Placeholder - will be implemented with new layout


float calculateResistance(float voltage, float loadResistor = 10.0) {
  return loadResistor * (5.0 - voltage) / voltage;
}


float calculateMQ3_PPM(float voltage) {
  float rs = calculateResistance(voltage);
  float ratio = rs / MQ3_RO;
  float ppm = MQ3_A * pow(ratio, MQ3_B);
  return ppm;
}

float calculateMQ135_PPM(float voltage) {
  float rs = calculateResistance(voltage);
  float ratio = rs / MQ135_RO;
  float ppm = MQ135_A * pow(ratio, MQ135_B);
  return ppm;
}

bool initializeADS() {
  static bool adsInitialized = false;
  if (adsInitialized) {
    return true;
  }

  Wire.begin(ADC_SDA_PIN, ADC_SCL_PIN);
  if (!ads.begin()) {
    Serial.println("Failed to initialize ADS.");
    return false;
  }

  ads.setGain(GAIN_ONE);
  adsInitialized = true;
  return true;
}

calibrationData runCalibration(uint16_t sampleCount = 50, uint32_t sampleDelayMs = 100, float loadResistor = 10.0f) {
  calibrationData result = {0.0f, 0.0f, false};

  if (!initializeADS()) {
    return result;
  }

  float sumV0 = 0.0f;
  float sumV1 = 0.0f;

  for (uint16_t i = 0; i < sampleCount; i++) {
    float v0 = ads.computeVolts(ads.readADC_SingleEnded(0)) * DIVIDER_RATIO;
    float v1 = ads.computeVolts(ads.readADC_SingleEnded(1)) * DIVIDER_RATIO;
    sumV0 += v0;
    sumV1 += v1;
    delay(sampleDelayMs);
  }

  result.avg_v0 = sumV0 / sampleCount;
  result.avg_v1 = sumV1 / sampleCount;
  result.valid = true;

  float suggestedMQ3_R0 = loadResistor * (5.0f - result.avg_v0) / result.avg_v0;
  float suggestedMQ135_R0 = loadResistor * (5.0f - result.avg_v1) / result.avg_v1;

  Serial.println("=== Calibration (clean air) ===");
  Serial.print("Average V0: ");
  Serial.println(result.avg_v0, 4);
  Serial.print("Average V1: ");
  Serial.println(result.avg_v1, 4);
  Serial.print("Suggested MQ3_RO: ");
  Serial.println(suggestedMQ3_R0, 2);
  Serial.print("Suggested MQ135_RO: ");
  Serial.println(suggestedMQ135_R0, 2);

  return result;
}

void blinkLed(const CRGB &color, uint32_t durationMs) {
  leds[0] = color;
  FastLED.show();
  delay(durationMs);
  leds[0] = CRGB::Black;
  FastLED.show();
}

void dht22Task(void *parameter) {
  while(1) {
    dhtData data;
    sensors_event_t event;
    dht.temperature().getEvent(&event);
    
    // error handling for DHT22
    if (isnan(event.temperature)) {
      Serial.println(F("Error reading temperature!"));
    } else {
      data.temperature = event.temperature;
      // Serial.print(event.temperature);
      // Serial.println("C");
    }
    
    dht.humidity().getEvent(&event);
    if (isnan(event.relative_humidity)) {
      Serial.println(F("Error reading temperature!"));
    } else {
      data.humidity = event.relative_humidity;
      // Serial.print(event.relative_humidity);
      // Serial.println("%");
    }

    // send data to queue
    xQueueSend(dhtQueue, &data, 0);
    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
}

void adsTask(void *parameter) {
  if (!initializeADS()) {
    while(1);
  }

  adsData data;

  while(1) {
    float volts0, volts1;
    volts0 = ads.computeVolts(ads.readADC_SingleEnded(0));
    volts1 = ads.computeVolts(ads.readADC_SingleEnded(1));

    data.alcohol_ppm = calculateMQ3_PPM(volts0 * DIVIDER_RATIO);
    data.air_quality_ppm = calculateMQ135_PPM(volts1 * DIVIDER_RATIO);

    // push data to queue (no Serial.print here to avoid conflicts)
    xQueueSend(adsQueue, &data, 0);
    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }

}

// ============ DISPLAY TASK (Core 1) ============
void displayTask(void *parameter) {
  dhtData dht_data;
  adsData ads_data;
  static uint32_t last_display_update_ms = 0;
  static uint32_t last_debug_ms = 0;
  
  Serial.println("[Display Task] Started on Core 1");
  vTaskDelay(1000 / portTICK_PERIOD_MS);  // Wait for everything to stabilize
  
  while(1) {
    // Receive latest sensor data from queues (non-blocking)
    if (xQueueReceive(dhtQueue, &dht_data, 0)) {
      current_readings.temperature = dht_data.temperature;
      current_readings.humidity = dht_data.humidity;
    }
    
    if (xQueueReceive(adsQueue, &ads_data, 0)) {
      current_readings.mq3_adc = ads_data.alcohol_ppm;  // Use PPM value as proxy
      current_readings.mq135_adc = ads_data.air_quality_ppm;
    }
    
    // Debug: print sensor data every 10 seconds
    if (millis() - last_debug_ms >= 10000) {
      Serial.printf("[Sensor] Temp=%.1f C, Humidity=%.1f %%, MQ3=%.1f ppm, MQ135=%.1f ppm\n",
                    current_readings.temperature, current_readings.humidity,
                    current_readings.mq3_adc, current_readings.mq135_adc);
      last_debug_ms = millis();
    }
    
    // Add to rolling buffer
    addToReadingBuffer(current_readings.mq3_adc, current_readings.mq135_adc, 
                       current_readings.temperature, current_readings.humidity);
    
    // ========== BUTTON HANDLING: Check for 3-second hold ==========
    if (button_pressed) {
      uint32_t hold_time = millis() - button_press_time;
      if (hold_time >= BASELINE_HOLD_TIME_MS) {
        // Get averaged readings from buffer
        float mq3_avg, mq135_avg, temp_avg, humidity_avg;
        getAveragedReadings(mq3_avg, mq135_avg, temp_avg, humidity_avg);
        
        // Save as baseline
        saveBaseline(mq3_avg, mq135_avg, temp_avg, humidity_avg);
        Serial.printf("[Button] Baseline saved! MQ3=%.1f, MQ135=%.1f, Temp=%.1f C\n",
                      mq3_avg, mq135_avg, temp_avg);
        blinkLed(CRGB::Green, 500);
        
        button_pressed = false;
      }
    }
    
    // ========== CALCULATE VERDICTS ==========
    if (baseline.is_valid) {
      // Get current averaged readings
      float mq3_avg, mq135_avg, temp_avg, humidity_avg;
      getAveragedReadings(mq3_avg, mq135_avg, temp_avg, humidity_avg);
      
      // Calculate deltas
      current_readings.mq3_delta = mq3_avg - baseline.mq3_baseline;
      current_readings.mq135_delta = mq135_avg - baseline.mq135_baseline;
      current_readings.temp_drift = temp_avg - baseline.baseline_temp;
      
      // Check temperature drift warning
      current_readings.temp_drift_warning = (fabs(current_readings.temp_drift) > 5.0f);
      
      // Calculate verdict score
      calculateVerdictFromScore(current_readings.rot_score, current_readings.verdict);
      
      // ========== UPDATE DISPLAY ==========
      if (millis() - last_display_update_ms >= 5000) {  // Update every 5 seconds
        // Get verdict text
        const char* verdict_str = "";
        switch (current_readings.verdict) {
          case FRESH:
            verdict_str = "FRESH";
            break;
          case MONITOR:
            verdict_str = "MONITOR";
            break;
          case SUSPECT:
            verdict_str = "SUSPECT";
            break;
          case ROTTEN:
            verdict_str = "ROTTEN";
            break;
          default:
            verdict_str = "UNCAL";
        }
        
        // Update display values
        updateDisplayValues(
          baseline.mq3_baseline, baseline.mq135_baseline, baseline.baseline_temp, baseline.baseline_humidity,
          mq3_avg, mq135_avg, temp_avg, humidity_avg,
          verdict_str
        );
        
        // Update LED status
        if (current_readings.verdict == ROTTEN) {
          leds[0] = CRGB::Red;
        } else if (current_readings.verdict == SUSPECT) {
          leds[0] = CRGB::Orange;
        } else if (current_readings.verdict == MONITOR) {
          leds[0] = CRGB::Yellow;
        } else {
          leds[0] = CRGB::Green;
        }
        FastLED.show();
        
        last_display_update_ms = millis();
      }
    } else {
      // No baseline - display UNCALIBRATED
      current_readings.verdict = UNCALIBRATED;
      if (millis() - last_display_update_ms >= 5000) {  // Update every 5 seconds
        updateDisplayValues(0, 0, 0, 0, 0, 0, 0, 0, "UNCAL");
        leds[0] = CRGB::Blue;  // Blue = waiting for baseline
        FastLED.show();
        last_display_update_ms = millis();
      }
    }
    
    vTaskDelay(500 / portTICK_PERIOD_MS);  // Check every 500ms
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);  // Wait for serial to stabilize
  Serial.println("\n\n[Setup] Starting initialization...");
  
  dht.begin();
  delay(500);
  
  // ========== TFT DISPLAY INITIALIZATION ==========
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);  // Enable backlight
  Serial.println("[TFT] Backlight enabled");
  
  delay(100);
  tft.init(240, 320);  // ST7789: width=240, height=320
  delay(100);
  tft.setRotation(0);  // 0=0°, 1=90°, 2=180°, 3=270° (try 0 or 1)
  delay(100);
  tft.fillScreen(ST77XX_BLACK);
  Serial.println("[TFT] Display initialized and cleared");
  
  // Draw initial layout
  drawReadings_Screen();
  Serial.println("[TFT] Initial layout drawn");
  
  // ========== NVS BASELINE LOADING ==========
  loadBaseline();
  
  // ========== BUTTON SETUP ==========
  pinMode(BASELINE_BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BASELINE_BUTTON_PIN), buttonISR, FALLING);
  Serial.println("[Button] Baseline button attached");
  
  // FastLED
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);
  blinkLed(CRGB::Green, 300);

  // RTOS things
  dhtQueue = xQueueCreate(1, sizeof(dhtData));
  adsQueue = xQueueCreate(1, sizeof(adsData));
  
  // tasks (Core 0 = sensor reading, Core 1 = display)
  xTaskCreatePinnedToCore(dht22Task, "DHT22", 2048, NULL, 10, NULL, 0);
  xTaskCreatePinnedToCore(adsTask, "ADS", 2048, NULL, 10, NULL, 0);
  xTaskCreatePinnedToCore(displayTask, "Display", 4096, NULL, 1, NULL, 1);
  
  Serial.println("[Setup] Complete - Dual-core RTOS running");
}

void loop() {
  // RTOS handles everything
  delay(1000);
}