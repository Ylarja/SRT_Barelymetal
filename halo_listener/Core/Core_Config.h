#ifndef CORE_CONFIG_H
#define CORE_CONFIG_H

#ifdef __cplusplus
    #define CONFIG_ASSERT(cond, msg) static_assert(cond, msg)
#else
    #define CONFIG_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif



#ifdef __cplusplus
extern "C" {
#endif


/* ********************************************************************************* */
/*                          USER CONFIGURABLE PARAMETERS                             */
/* ********************************************************************************* */


/* TARGET DEVICE CONFIGURATION */
#define __TARGET_DEVICE_ZCU104__
//#define __TARGET_DEVICE_ADAFRUIT_CAN_BUS_FEATHER__
//#define __TARGET_DEVICE_PORTENTA_H7__



/* CONSOLE CONFIGURATION */
//#define __TARGET_PLATFORM_ARDUINO__
#define __TARGET_PLATFORM_POSIX__
//#define __TARGET_PLATFORM_WINDOWS__
//#define __TARGET_PLATFORM_LOCAL__



////#define CAN_DEFAULT_BAUDRATE 250000U


/* DIAGNOSTIC CONFIGURATION */
#define DIAG_LOG_LEVEL DIAG_LVL_INFO
#define DIAG_LOG_MODE_RING_BUFFER
#define DIAG_LOG_MODE_VIRTUAL_SERIAL
#define DIAG_EVENT_MAX_PARAMS 3
#define DIAG_LOG_SIZE 255
#define HALO_DISABLE_LEDS

/* ********************************************************************************* */
/*                  INTERNAL CONFIGURATION PARAMETERS                                */
/*                  DO NOT MODIFY THESE PARAMETERS                                   */
/* ********************************************************************************* */









/* VALIDATION */


//CONFIG_ASSERT(CAN_DEFAULT_BAUDRATE == 1000000U || CAN_DEFAULT_BAUDRATE == 500000U || CAN_DEFAULT_BAUDRATE == 250000U || CAN_DEFAULT_BAUDRATE == ////125000U, "CAN_DEFAULT_BAUDRATE must be one of the standard baud rates: 125000, 250000, 500000, or 1000000 bps");



#ifdef __cplusplus
}
#endif

#endif // CORE_CONFIG_H