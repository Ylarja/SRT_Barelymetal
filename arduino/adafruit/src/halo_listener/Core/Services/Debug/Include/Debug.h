#ifndef DEBUG_SERVICE_H
#define DEBUG_SERVICE_H
#include <stdio.h>
#include "../../../Core_Return_Codes.h"

// =======================
// Compile-time debug level
// =======================
// 0 = off
// 1 = errors
// 2 = info
// 3 = trace

#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL 0
#endif

#if DEBUG_LEVEL > 0
void Debug_Write(const char *fmt, ...);
const char* RC_To_String(return_code_t rc);
#endif

// =======================
// Debug macros
// =======================

#if DEBUG_LEVEL >= 1
#define DEBUG_LOG_ERROR(...)   Debug_Write("[ERROR] " __VA_ARGS__)
#else
#define DEBUG_LOG_ERROR(...)   ((void)0)
#endif

#if DEBUG_LEVEL >= 2
#define DEBUG_LOG_INFO(...)  Debug_Write("[INFO]  " __VA_ARGS__)
#else
#define DEBUG_LOG_INFO(...)  ((void)0)
#endif

#if DEBUG_LEVEL >= 3
#define DEBUG_LOG_TRACE(...) Debug_Write("[TRACE] " __VA_ARGS__)
#else
#define DEBUG_LOG_TRACE(...) ((void)0)
#endif

#endif // DEBUG_SERVICE_H