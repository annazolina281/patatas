#ifndef POTATO_DATA_H
#define POTATO_DATA_H

#include <cstdint>

// Verdict enum
enum VerdictState {
  UNCALIBRATED = 0,
  FRESH = 1,
  MONITOR = 2,
  SUSPECT = 3,
  ROTTEN = 4
};

// Sensor reading data with deltas and scoring
struct SensorReadings {
  // Raw sensor values
  float mq3_adc;           // Raw ADC value from MQ3
  float mq135_adc;         // Raw ADC value from MQ135
  float temperature;       // DHT22 temperature
  float humidity;          // DHT22 humidity
  
  // Deltas from baseline
  float mq3_delta;         // Current - baseline for MQ3
  float mq135_delta;       // Current - baseline for MQ135
  float temp_drift;        // Current temp - baseline temp
  
  // Scoring and verdict
  int rot_score;           // 0-6+ based on algorithm
  VerdictState verdict;    // Current verdict
  bool temp_drift_warning; // True if temp diff > 5°C
  bool baseline_set;       // True if baseline has been calibrated
  
  // Tracking for hysteresis
  uint32_t verdict_count;  // Consecutive readings at current score
};

// Baseline data stored in NVS
struct BaselineData {
  float mq3_baseline;      // ADC value at baseline
  float mq135_baseline;    // ADC value at baseline
  float baseline_temp;     // Temperature at baseline time
  float baseline_humidity; // Humidity at baseline time
  uint32_t baseline_timestamp; // Unix timestamp when baseline was set
  bool is_valid;           // True if baseline has been set
};

#endif // POTATO_DATA_H
