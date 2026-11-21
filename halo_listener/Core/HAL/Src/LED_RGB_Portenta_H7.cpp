#include "../../Core_Config.h"

#if defined(__TARGET_DEVICE_PORTENTA_H7__)

#include <Arduino.h>
#include "../Include/LED_RGB.h"

// Define RGB LED pins (choose any free PWM-capable pins)
const int RED_PIN   = LEDR;
const int GREEN_PIN = LEDG;
const int BLUE_PIN  = LEDB;

// Setup
void HAL_LED_RGB_Init() {
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);

}

void HAL_LED_RGB_Toggle_Red() {
  digitalWrite(RED_PIN, !digitalRead(RED_PIN));
}

void HAL_LED_RGB_Toggle_Green() {
  digitalWrite(GREEN_PIN, !digitalRead(GREEN_PIN));
}

void HAL_LED_RGB_Toggle_Blue() {
  digitalWrite(BLUE_PIN, !digitalRead(BLUE_PIN));
}

void HAL_LED_RGB_Red_On() {
  digitalWrite(RED_PIN, LOW);
}

void HAL_LED_RGB_Red_Off() {
  digitalWrite(RED_PIN, HIGH);
}

void HAL_LED_RGB_Green_On() {
  digitalWrite(GREEN_PIN, LOW);
}

void HAL_LED_RGB_Green_Off() {
  digitalWrite(GREEN_PIN, HIGH);
}

void HAL_LED_RGB_Blue_On() {
  digitalWrite(BLUE_PIN, LOW);
}

void HAL_LED_RGB_Blue_Off() {
  digitalWrite(BLUE_PIN, HIGH);
}

void HAL_LED_RGB_Off() {
  digitalWrite(RED_PIN, HIGH);
  digitalWrite(GREEN_PIN, HIGH);
  digitalWrite(BLUE_PIN, HIGH);
}

void HAL_LED_RGB_On() {
  digitalWrite(RED_PIN, LOW);
  digitalWrite(GREEN_PIN, LOW);
  digitalWrite(BLUE_PIN, LOW);
}


bool HAL_LED_RGB_Is_Red_On() {
  return (digitalRead(RED_PIN) == LOW);
}

bool HAL_LED_RGB_Is_Green_On() {
  return (digitalRead(GREEN_PIN) == LOW);
}

bool HAL_LED_RGB_Is_Blue_On() {
  return (digitalRead(BLUE_PIN) == LOW);
}

#endif // __TARGET_DEVICE_PORTENTA_H7__