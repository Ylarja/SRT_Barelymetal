
#include "../../Core_Config.h"
#ifdef __TARGET_PLATFORM_POSIX__
#include "../Include/Software_Clock.h"
#include <time.h>

uint64_t HAL_Get_Micros(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts); // monotonic avoids jumps
    return (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000ULL;
}

#elif defined(__TARGET_PLATFORM_WINDOWS__)
#include <windows.h>
#include "../Include/Software_Clock.h"
uint64_t HAL_Get_Micros(void) {

    static LARGE_INTEGER freq = {0};
    LARGE_INTEGER counter;

    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
    }

    QueryPerformanceCounter(&counter);
    return (uint64_t)((counter.QuadPart * 1000000ULL) / freq.QuadPart);
}
#elif defined(__TARGET_PLATFORM_ARDUINO__)
#include <Arduino.h>
#include "../Include/Software_Clock.h"
uint64_t HAL_Get_Micros(void) {
    return (uint64_t)::micros(); // built-in Arduino function
}

#else
#error "Platform not supported"
#endif
