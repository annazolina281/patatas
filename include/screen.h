// [BEGIN lopaka generated]
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Adafruit_ST7789.h>

extern Adafruit_ST7789 tft;  // Declare extern - defined in main.cpp


void drawScreen_1(void) {
    tft.fillScreen(ST77XX_BLACK);

    // Title
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(2);
    tft.setFont(&FreeSansBold9pt7b);
    tft.setCursor(20, 38);
    tft.print("Patatas");

    // Title copy 1
    tft.setCursor(20, 68);
    tft.print("Checker");

    // Baseline Readings
    tft.setTextSize(1);
    tft.setFont(&FreeSans9pt7b);
    tft.setCursor(20, 117);
    tft.print("Baseline Readings:");

    // Baseline MQ3 label
    tft.setCursor(20, 137);
    tft.print("MQ3: ");

    // Baseline MQ3 value
    tft.setCursor(70, 137);
    tft.print("100");

    // Baseline MQ135 label
    tft.setCursor(120, 137);
    tft.print("MQ135: ");

    // Baseline MQ135 value
    tft.setCursor(190, 137);
    tft.print("100");

    // Baseline Temp label
    tft.setCursor(21, 157);
    tft.print("Temp:");

    // Baseline Temp value
    tft.setCursor(70, 157);
    tft.print("100");

    // Baseline Humidity label
    tft.setCursor(120, 157);
    tft.print("Humi:");

    // Baseline Humidity value
    tft.setCursor(168, 157);
    tft.print("100");

    // Previous Readings
    tft.setCursor(20, 197);
    tft.print("Previous Readings:");

    // Previous MQ3
    tft.setCursor(20, 217);
    tft.print("MQ3: ");

    // Previous MQ3 value
    tft.setCursor(70, 217);
    tft.print("100");

    // Previous MQ135 label
    tft.setCursor(120, 217);
    tft.print("MQ135: ");

    // Previous MQ135 value
    tft.setCursor(190, 217);
    tft.print("100");

    // Previous Temp label
    tft.setCursor(20, 237);
    tft.print("Temp:");

    // Previous Temp value
    tft.setCursor(70, 237);
    tft.print("100");

    // Previous Humidity label
    tft.setCursor(120, 237);
    tft.print("Humi:");

    // Previous Humidity value
    tft.setCursor(168, 237);
    tft.print("100");

    // Status Label
    tft.setFont(&FreeSans12pt7b);
    tft.setCursor(20, 296);
    tft.print("Status:");

    // Status value
    tft.setFont(&FreeSansBold12pt7b);
    tft.setCursor(98, 296);
    tft.print("Text");
}

// Wrapper function for compatibility with main.cpp
void drawReadings_Screen(void) {
    drawScreen_1();
}

// Update function - only refreshes the value display areas
void updateDisplayValues(float baseline_mq3, float baseline_mq135, float baseline_temp, float baseline_humidity,
                         float previous_mq3, float previous_mq135, float previous_temp, float previous_humidity,
                         const char* status_text) {
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(1);
    tft.setFont(&FreeSans9pt7b);
    
    // Clear and update Baseline MQ3 value
    tft.fillRect(70, 125, 40, 20, ST77XX_BLACK);
    tft.setCursor(70, 137);
    tft.print((int)baseline_mq3);
    
    // Clear and update Baseline MQ135 value
    tft.fillRect(190, 125, 40, 20, ST77XX_BLACK);
    tft.setCursor(190, 137);
    tft.print((int)baseline_mq135);
    
    // Clear and update Baseline Temp value
    tft.fillRect(70, 145, 40, 20, ST77XX_BLACK);
    tft.setCursor(70, 157);
    tft.print((int)baseline_temp);
    
    // Clear and update Baseline Humidity value
    tft.fillRect(168, 145, 40, 20, ST77XX_BLACK);
    tft.setCursor(168, 157);
    tft.print((int)baseline_humidity);
    
    // Clear and update Previous MQ3 value
    tft.fillRect(70, 205, 40, 20, ST77XX_BLACK);
    tft.setCursor(70, 217);
    tft.print((int)previous_mq3);
    
    // Clear and update Previous MQ135 value
    tft.fillRect(190, 205, 40, 20, ST77XX_BLACK);
    tft.setCursor(190, 217);
    tft.print((int)previous_mq135);
    
    // Clear and update Previous Temp value
    tft.fillRect(70, 225, 40, 20, ST77XX_BLACK);
    tft.setCursor(70, 237);
    tft.print((int)previous_temp);
    
    // Clear and update Previous Humidity value
    tft.fillRect(168, 225, 40, 20, ST77XX_BLACK);
    tft.setCursor(168, 237);
    tft.print((int)previous_humidity);
    
    // Clear and update Status value
    tft.setFont(&FreeSansBold12pt7b);
    tft.fillRect(98, 284, 130, 24, ST77XX_BLACK);
    tft.setCursor(98, 296);
    tft.print(status_text);
}
// [END lopaka generated]
