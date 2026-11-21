// Auto-generated header
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "DOL_Types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const object_desc_t* const descriptor;
    uint8_t text[21];
} test_msg_t;

typedef struct {
    double r1;
    double r2;
} sensor_blue_t;

typedef struct {
    const object_desc_t* const descriptor;
    uint16_t id;
    sensor_blue_t sensor_blue;
} test_msg2_t;

extern const object_desc_t TEST_MSG_DESCRIPTOR;
extern const object_desc_t TEST_MSG2_DESCRIPTOR;

extern const test_msg_t TEST_MSG_INIT;
extern const test_msg2_t TEST_MSG2_INIT;

#ifdef __cplusplus
}
#endif
