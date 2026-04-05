#include <Arduino.h>
#include <secrets.h>

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

// physical pins
#define DHT22_PIN 10
#define ADC_SDA_PIN 8
#define ADC_SCL_PIN 9

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

// the ACTUAL cool stuff (RTOS)
QueueHandle_t dhtQueue, adsQueue;

const float DIVIDER_RATIO = 1.5; // inverse of 2/(1+2) in kR

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
    
    // Print raw voltages (after divider compensation)
    Serial.print("V0: ");
    Serial.print(volts0 * DIVIDER_RATIO);
    Serial.print("V    |    V1: ");
    Serial.print(volts1 * DIVIDER_RATIO);
    Serial.print("V    |");

    data.alcohol_ppm = calculateMQ3_PPM(volts0 * DIVIDER_RATIO);
    data.air_quality_ppm = calculateMQ135_PPM(volts1 * DIVIDER_RATIO);
    
    Serial.print("PPM = MQ-3 : ");
    Serial.print(calculateMQ3_PPM(volts0 * DIVIDER_RATIO));
    Serial.print("    |    MQ 135:");
    Serial.println(calculateMQ135_PPM(volts1 * DIVIDER_RATIO));

    // push data to queue
    xQueueSend(adsQueue, &data, 0);
    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }

}


void setup() {
  Serial.begin(115200);
  dht.begin();
  delay(500);
  //FastLED
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);
  blinkLed(CRGB::Green, 300);

  // Optional one-shot calibration in clean air before starting tasks.
  // calibrationData cal = runCalibration();

  // RTOS things
  dhtQueue = xQueueCreate(5, sizeof(dhtData));
  adsQueue = xQueueCreate(5, sizeof(adsData));
  // tasks
  xTaskCreatePinnedToCore(dht22Task, "DHT22", 2048, NULL, 10, NULL, 1);
  xTaskCreatePinnedToCore(adsTask, "ADS", 2048, NULL, 10, NULL, 1);
}

void loop() {
  // hehe wala na po ate!
}