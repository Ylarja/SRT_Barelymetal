#include "../Include/Time.h"


uint64_t Get_Micros(void) {

    return HAL_Get_Micros();
}

uint64_t Get_Millis(void) {

    return HAL_Get_Micros() / 1000ULL;
}

uint64_t Get_Seconds(void) {

    return HAL_Get_Micros() / 1000000ULL;
}

void Delay_Micros(uint64_t micros) {

    uint64_t start = HAL_Get_Micros();
    while ((HAL_Get_Micros() - start) < micros) {
        // busy wait
    }
}

void Delay_Millis(uint64_t millis) {

    uint64_t start = HAL_Get_Micros();
    while ((HAL_Get_Micros() - start) < (millis * 1000ULL)) {
        // busy wait
    }
}

void Delay_Seconds(uint64_t seconds) {

    uint64_t start = HAL_Get_Micros();
    while ((HAL_Get_Micros() - start) < (seconds * 1000000ULL)) {
        // busy wait
    }
}