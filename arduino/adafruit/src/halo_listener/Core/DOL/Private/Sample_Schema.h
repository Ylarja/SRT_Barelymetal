// Auto-generated header
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "DOL_Types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float temperature;
    float pressure;
    float humidity;
} baro_sensor_t;

typedef struct {
    float obj_dist[8];
    float altitude;
    uint32_t light_levels[3];
} lidar_sensor_t;

typedef struct {
    double attitude[3];
    float heading;
} imu_sensor_t;

typedef struct {
    const object_desc_t* const descriptor;
    uint32_t id;
    float status;
    baro_sensor_t baro_sensor;
    lidar_sensor_t lidar_sensor;
    imu_sensor_t imu_sensor[3];
    uint8_t message[50];
    uint64_t timestamp;
} sample_object_t;

extern const object_desc_t SAMPLE_OBJECT_DESCRIPTOR;

extern const sample_object_t SAMPLE_OBJECT_INIT;

#ifdef __cplusplus
}
#endif
