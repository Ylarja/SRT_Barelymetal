#ifndef CORE_H
#define CORE_H

#ifdef __cplusplus
extern "C" {    
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "Core_Return_Codes.h"

#include "Services/Diagnostic/Include/Diagnostic.h"
#include "Services/SerDes/Include/SerDes.h"
#include "Services/Time/Include/Time.h"
#include "Services/Buffers/Include/Ring_Buffer.h"
#include "Services/Buffers/Include/LIFO_Buffer.h"
#include "Services/Debug/Include/Debug.h"

#include "HAL/Include/CAN.h"
#include "HAL/Include/Virtual_Serial.h"
#include "HAL/Include/Software_Clock.h"
#include "HAL/Include/LED_RGB.h"

/**
 * @brief Portable critical section macros.
 * Disable/restore interrupts around shared-state accesses between ISR and task context.
 * Uses CMSIS __disable_irq/__enable_irq on all Cortex-M targets (M0, M0+, M3, M4, M7),
 * which covers both Portenta H7 (STM32H7, Cortex-M7) and Adafruit CAN Bus Feather
 * (SAMD21, Cortex-M0+). Falls back to no-op on unknown targets.
 * Usage:
 *   CRITICAL_SECTION_ENTER();
 *   ... access shared state ...
 *   CRITICAL_SECTION_EXIT();
 **/
#if defined(__CORTEX_M)
  #define CRITICAL_SECTION_ENTER() __disable_irq()
  #define CRITICAL_SECTION_EXIT()  __enable_irq()
#elif defined(__TARGET_PLATFORM_POSIX__)
  #include <pthread.h>
  extern pthread_mutex_t core_critical_section_mutex;
  #define CRITICAL_SECTION_ENTER() pthread_mutex_lock(&core_critical_section_mutex)
  #define CRITICAL_SECTION_EXIT()  pthread_mutex_unlock(&core_critical_section_mutex)
#else
  #define CRITICAL_SECTION_ENTER() ((void)0)
  #define CRITICAL_SECTION_EXIT()  ((void)0)
#endif




#ifdef __cplusplus
}
#endif

#endif // CORE_H