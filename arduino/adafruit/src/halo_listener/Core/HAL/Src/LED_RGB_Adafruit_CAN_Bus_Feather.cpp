#include "../../Core_Config.h"

#if defined(__TARGET_DEVICE_ADAFRUIT_CAN_BUS_FEATHER__)

#include <Arduino.h>
#include "../Include/LED_RGB.h"
#include <Adafruit_NeoPixel.h>

#define PIN       PIN_NEOPIXEL
#define NUMPIXELS 3
Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

// Setup
void HAL_LED_RGB_Init() {
  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, HIGH);
  pixels.begin();
  pixels.show(); 
}

void HAL_LED_RGB_Toggle_Red() {
  if (pixels.getPixelColor(0) == pixels.Color(255, 0, 0)) {
    pixels.setPixelColor(0, pixels.Color(0, 0, 0)); // Turn off
  } else {
    pixels.setPixelColor(0, pixels.Color(255, 0, 0)); // Red
  }
  pixels.show();
}

void HAL_LED_RGB_Toggle_Green() {
  if (pixels.getPixelColor(0) == pixels.Color(0, 255, 0)) {
    pixels.setPixelColor(0, pixels.Color(0, 0, 0)); // Turn off
  } else {
    pixels.setPixelColor(0, pixels.Color(0, 255, 0)); // Green
  }
  pixels.show();
}

void HAL_LED_RGB_Toggle_Blue() {
  if (pixels.getPixelColor(0) == pixels.Color(0, 0, 255)) {
    pixels.setPixelColor(0, pixels.Color(0, 0, 0)); // Turn off
  } else {
    pixels.setPixelColor(0, pixels.Color(0, 0, 255)); // Blue
  }
  pixels.show();
}

void HAL_LED_RGB_Red_On() {
  pixels.setPixelColor(0, pixels.Color(255, 0, 0)); // Red
  pixels.show();
}

void HAL_LED_RGB_Red_Off() {
  pixels.setPixelColor(0, pixels.Color(0, 0, 0)); // Turn off
  pixels.show();
}

void HAL_LED_RGB_Green_On() {
  pixels.setPixelColor(0, pixels.Color(0, 255, 0)); // Green
  pixels.show();
}

void HAL_LED_RGB_Green_Off() {
  pixels.setPixelColor(0, pixels.Color(0, 0, 0)); // Turn off
  pixels.show();
}

void HAL_LED_RGB_Blue_On() {
  pixels.setPixelColor(0, pixels.Color(0, 0, 255)); // Blue
  pixels.show();
}

void HAL_LED_RGB_Blue_Off() {
  pixels.setPixelColor(0, pixels.Color(0, 0, 0)); // Turn off
  pixels.show();
}

void HAL_LED_RGB_Off() {
  pixels.setPixelColor(0, pixels.Color(0, 0, 0)); // Turn off
  pixels.show();
}

void HAL_LED_RGB_On() {
  pixels.setPixelColor(0, pixels.Color(255, 255, 255)); // White
  pixels.show();
}

bool HAL_LED_RGB_Is_Red_On() {
  return (pixels.getPixelColor(0) & pixels.Color(255, 0, 0)) != 0;
}

bool HAL_LED_RGB_Is_Green_On() {
  return (pixels.getPixelColor(0) & pixels.Color(0, 255, 0)) != 0;
}

bool HAL_LED_RGB_Is_Blue_On() {
  return (pixels.getPixelColor(0) & pixels.Color(0, 0, 255)) != 0;
}

#endif // __TARGET_DEVICE_ADAFRUIT_CAN_BUS_FEATHER__