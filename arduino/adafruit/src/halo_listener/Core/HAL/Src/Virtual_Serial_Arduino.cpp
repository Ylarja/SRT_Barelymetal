#include "../../Core_Config.h"

#ifdef __TARGET_PLATFORM_ARDUINO__

#include "../Include/Virtual_Serial.h"
#include <Arduino.h>

return_code_t HAL_Virtual_Serial_Init(uint32_t baudrate) {

  if(Serial){
    return RC_OK; // Serial already initialized
  }

  if (baudrate != 9600 && baudrate != 19200 && baudrate != 38400 && baudrate != 57600 && baudrate != 115200) {
    return RC_INVALID_ARG;
  }

  if (!Serial) {
    Serial.begin(baudrate);
    Delay_Millis(100);
    if (!Serial) {
      return RC_HW_FAULT;
    }
  }

    return RC_OK;
}

return_code_t HAL_Virtual_Serial_Write(const char *msg) {

  if (msg == NULL) {
    return RC_INVALID_ARG;
  }

  if (!Serial) {
    return RC_HW_FAULT;
  }

  Serial.print(msg);

  return RC_OK;
}

return_code_t HAL_Virtual_Serial_Read(const char *msg) {

  if (msg == NULL) {
    return RC_INVALID_ARG;
  }

  if (!Serial) {
    return RC_HW_FAULT;
  }

  if (Serial.available()) {

    String received = Serial.readStringUntil('\n');
    received.toCharArray((char *)msg, received.length() + 1);
    return RC_OK;
  }
  return RC_OK_NO_DATA;
}

#endif // __VIRTUAL_SERIAL_ARDUINO__