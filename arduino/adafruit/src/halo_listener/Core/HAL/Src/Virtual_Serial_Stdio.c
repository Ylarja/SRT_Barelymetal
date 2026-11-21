#include "../../Core_Config.h"

#if defined __TARGET_PLATFORM_LOCAL__  || defined __TARGET_PLATFORM_POSIX__ 

#include "../Include/Virtual_Serial.h"
#include <stdio.h>


return_code_t HAL_Virtual_Serial_Init(uint32_t baudrate){
    // No initialization needed for local console
    (void)baudrate; // Suppress unused parameter warning
    return RC_OK;
}

return_code_t HAL_Virtual_Serial_Write(char* msg){

    if(msg == NULL){
        return RC_INVALID_ARG;
    }

    printf("%s\n", msg);
    return RC_OK;
}

return_code_t HAL_Virtual_Serial_Read(char* msg){
    if(msg == NULL){
        return RC_INVALID_ARG;
    }
    scanf("%s", msg);
    return RC_OK;
}

#endif // __TARGET_PLATFORM_LOCAL__