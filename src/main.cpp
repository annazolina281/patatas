#include <Arduino.h>
#include <secrets.h>
#include <WiFi.h>
#include <WiFiClient.h>

// libraries and definitions
#include <BlynkSimpleEsp32.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#define DHTTYPE DHT22

// physical pins
#define DHT22_PIN 10
#define ADC_SCA_PIN 8
#define ADC_SCL_PIN 9

// initializations
DHT_Unified dht(DHT22_PIN, DHTTYPE);
static WiFiClient blynkWiFiClient;

// structs
struct SensorData {
  float temperature;
  float humidity;
};

// the ACTUAL cool stuff (RTOS)
QueueHandle_t sensorQueue;

void dht22Task(void *parameter) {
  while(1) {
    SensorData data;
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
    xQueueSend(sensorQueue, &data, 0);

    // task delay
    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
}

void blynkTask(void *parameter) {
  // connect to wifi first
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(500 / portTICK_PERIOD_MS);
    Serial.println("Connecting to WiFi"); 
  }

  // then start Blynk
  // Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass, "blynk.cloud", 80);
  Blynk.config(BLYNK_AUTH_TOKEN, "blynk.cloud", 80);

  while(1) {  
    SensorData data;
    if (xQueueReceive(sensorQueue, &data, 500 / portTICK_PERIOD_MS)) {
      if (Blynk.connected()) {
        Blynk.virtualWrite(V0, data.temperature);
        Blynk.virtualWrite(V1, data.humidity);
        Serial.print("uploaded: ");
        Serial.print(data.temperature);
        Serial.println("°C, ");
        Serial.print(data.humidity);
        Serial.println("%");
      } else {
      }
    }

    Blynk.run();

    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);
  dht.begin();
  delay(500);
  
  // RTOS things
  sensorQueue = xQueueCreate(5, sizeof(SensorData));
  // tasks
  xTaskCreatePinnedToCore(dht22Task, "DHT22", 2048, NULL, 10, NULL, 1);
  xTaskCreatePinnedToCore(blynkTask, "Blynk", 8192, NULL, 10, NULL, 0);
}

void loop() {
  // hehe wala na po ate!
}