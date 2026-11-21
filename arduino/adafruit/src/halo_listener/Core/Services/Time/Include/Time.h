#ifndef CORE_TIME_H
#define CORE_TIME_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../../Core.h"

/****************************************************
 * @brief    Gets the current time in microseconds
 * 
 * @retval   Current time in microseconds
 ****************************************************/
uint64_t Get_Micros(void);

/****************************************************
 * @brief    Gets the current time in milliseconds
 * 
 * @retval   Current time in milliseconds
 ****************************************************/
uint64_t Get_Millis(void);

/****************************************************
 * @brief    Gets the current time in seconds
 * 
 * @retval   Current time in seconds
 ****************************************************/
uint64_t Get_Seconds(void);

/****************************************************
 * @brief    Delays execution for specified microseconds
 * 
 * @param    micros             Number of microseconds to delay
 ****************************************************/
void Delay_Micros(uint64_t micros);

/****************************************************
 * @brief    Delays execution for specified milliseconds
 * 
 * @param    millis             Number of milliseconds to delay
 ****************************************************/
void Delay_Millis(uint64_t millis);

/****************************************************
 * @brief    Delays execution for specified seconds
 * 
 * @param    seconds            Number of seconds to delay
 ****************************************************/
void Delay_Seconds(uint64_t seconds);

#ifdef __cplusplus
}
#endif

#endif // CORE_TIME_H