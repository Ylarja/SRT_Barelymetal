#include "../Private/Sample_Schema.h"

const field_desc_t SAMPLE_OBJECT_FIELDS_DESCRIPTOR[] = {
    {
        .element_size = sizeof(uint32_t),
        .element_count = 1,
        .inner_count = 1,
        .offset = offsetof(sample_object_t, id),
        .stride = sizeof(uint32_t)
    },
    {
        .element_size = sizeof(float),
        .element_count = 1,
        .inner_count = 1,
        .offset = offsetof(sample_object_t, status),
        .stride = sizeof(float)
    },
    {
        .element_size = sizeof(float),
        .element_count = 1,
        .inner_count = 1,
        .offset = offsetof(sample_object_t, baro_sensor) + offsetof(baro_sensor_t, temperature),
        .stride = sizeof(baro_sensor_t)
    },
    {
        .element_size = sizeof(float),
        .element_count = 1,
        .inner_count = 1,
        .offset = offsetof(sample_object_t, baro_sensor) + offsetof(baro_sensor_t, pressure),
        .stride = sizeof(baro_sensor_t)
    },
    {
        .element_size = sizeof(float),
        .element_count = 1,
        .inner_count = 1,
        .offset = offsetof(sample_object_t, baro_sensor) + offsetof(baro_sensor_t, humidity),
        .stride = sizeof(baro_sensor_t)
    },
    {
        .element_size = sizeof(float),
        .element_count = 8,
        .inner_count = 8,
        .offset = offsetof(sample_object_t, lidar_sensor) + offsetof(lidar_sensor_t, obj_dist),
        .stride = sizeof(lidar_sensor_t)
    },
    {
        .element_size = sizeof(float),
        .element_count = 1,
        .inner_count = 1,
        .offset = offsetof(sample_object_t, lidar_sensor) + offsetof(lidar_sensor_t, altitude),
        .stride = sizeof(lidar_sensor_t)
    },
    {
        .element_size = sizeof(uint32_t),
        .element_count = 3,
        .inner_count = 3,
        .offset = offsetof(sample_object_t, lidar_sensor) + offsetof(lidar_sensor_t, light_levels),
        .stride = sizeof(lidar_sensor_t)
    },
    {
        .element_size = sizeof(double),
        .element_count = 9,
        .inner_count = 3,
        .offset = offsetof(sample_object_t, imu_sensor) + offsetof(imu_sensor_t, attitude),
        .stride = sizeof(imu_sensor_t)
    },
    {
        .element_size = sizeof(float),
        .element_count = 3,
        .inner_count = 1,
        .offset = offsetof(sample_object_t, imu_sensor) + offsetof(imu_sensor_t, heading),
        .stride = sizeof(imu_sensor_t)
    },
    {
        .element_size = sizeof(uint8_t),
        .element_count = 50,
        .inner_count = 1,
        .offset = offsetof(sample_object_t, message),
        .stride = sizeof(uint8_t)
    },
    {
        .element_size = sizeof(uint64_t),
        .element_count = 1,
        .inner_count = 1,
        .offset = offsetof(sample_object_t, timestamp),
        .stride = sizeof(uint64_t)
    },
};

const object_desc_t SAMPLE_OBJECT_DESCRIPTOR = {
    .fields = SAMPLE_OBJECT_FIELDS_DESCRIPTOR,
    .field_count = sizeof(SAMPLE_OBJECT_FIELDS_DESCRIPTOR) / sizeof(field_desc_t),
    .message_size = 210,
    .object_size = sizeof(sample_object_t)
};

const sample_object_t SAMPLE_OBJECT_INIT = {
    .descriptor = &SAMPLE_OBJECT_DESCRIPTOR,
};

