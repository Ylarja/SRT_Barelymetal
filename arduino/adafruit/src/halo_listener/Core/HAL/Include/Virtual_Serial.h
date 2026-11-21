#ifndef _HAL_CONSOLE_H
#define _HAL_CONSOLE_H

#include "../../Core.h"

#ifdef __cplusplus
extern "C" {
#endif

/****************************************************
 * @brief    Initializes the virtual serial console
 * 
 * @param    baudrate           Baudrate for serial communication
 * 
 * @retval   RC_OK              Initialization successful
 * @retval   RC_INVALID_ARG     Unsupported baudrate
 * @retval   RC_HW_FAULT        Hardware initialization failed
 * 
 * @note     Supported baudrates: 9600, 19200, 38400, 57600, 115200
 ****************************************************/
return_code_t HAL_Virtual_Serial_Init(uint32_t baudrate);

/****************************************************
 * @brief    Writes a message to the virtual serial console
 * 
 * @param    msg                Null-terminated string message to write
 * 
 * @retval   RC_OK              Message written successfully
 * @retval   RC_INVALID_ARG     msg is NULL
 * @retval   RC_HW_FAULT        Hardware failure occurred
 ****************************************************/
return_code_t HAL_Virtual_Serial_Write(const char* msg);

/****************************************************
 * @brief    Reads a message from the virtual serial console
 * 
 * @param    msg                Buffer to store the read message
 * 
 * @retval   RC_OK              Message read successfully
 * @retval   RC_INVALID_ARG     msg is NULL
 * @retval   RC_HW_FAULT        Hardware failure occurred
 ****************************************************/
return_code_t HAL_Virtual_Serial_Read(char* msg);


#ifdef __cplusplus
}
#endif
#endif // _HAL_CONSOLE_H
