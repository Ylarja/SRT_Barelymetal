#ifndef HAL_SOFTWARE_CLOCK_H
#define HAL_SOFTWARE_CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../../Core.h"

/****************************************************
 * @brief    Gets the current time in microseconds
 * 
 * @retval   Current time in microseconds
 ****************************************************/
uint64_t HAL_Get_Micros(void);

#ifdef __cplusplus
}
#endif 

#endif // HAL_SOFTWARE_CLOCK_H