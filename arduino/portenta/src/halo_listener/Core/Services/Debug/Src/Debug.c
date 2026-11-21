#include "../Include/Debug.h"
#include <stdarg.h>

extern void HAL_Virtual_Serial_Write(char *msg);

void Debug_Write(const char *fmt, ...)
{
    char buffer[256]; 

    va_list args;
    va_start(args, fmt);

    vsnprintf(buffer, sizeof(buffer), fmt, args);

    va_end(args);

    HAL_Virtual_Serial_Write(buffer);
}

const char* RC_To_String(return_code_t rc)
{
    switch (rc)
    {
        case RC_OK:             return "RC_OK";
        case RC_OK_NO_DATA:     return "RC_OK_NO_DATA";
        case RC_OK_TRUE:        return "RC_OK_TRUE";
        case RC_OK_FALSE:       return "RC_OK_FALSE";

        case RC_TIMEOUT:        return "RC_TIMEOUT";
        case RC_BUSY:           return "RC_BUSY";
        case RC_RETRY_LIMIT:    return "RC_RETRY_LIMIT";
        case RC_INVALID_ARG:    return "RC_INVALID_ARG";
        case RC_NO_RESOURCE:    return "RC_NO_RESOURCE";
        case RC_PERM_DENIED:    return "RC_PERM_DENIED";
        case RC_HW_FAULT:       return "RC_HW_FAULT";
        case RC_INTEGRITY_FAIL: return "RC_INTEGRITY_FAIL";
        case RC_DATA_RANGE:     return "RC_DATA_RANGE";
        case RC_ERROR:          return "RC_ERROR";
        case RC_FATAL:          return "RC_FATAL";

        default:                return "RC_UNKNOWN";
    }
}
