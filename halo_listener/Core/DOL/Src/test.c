#include "../Private/test.h"

const field_desc_t TEST_MSG_FIELDS_DESCRIPTOR[] = {
    {
        .element_size = sizeof(uint8_t),
        .element_count = 21,
        .inner_count = 1,
        .offset = offsetof(test_msg_t, text),
        .stride = sizeof(uint8_t)
    },
};

const object_desc_t TEST_MSG_DESCRIPTOR = {
    .fields = TEST_MSG_FIELDS_DESCRIPTOR,
    .field_count = sizeof(TEST_MSG_FIELDS_DESCRIPTOR) / sizeof(field_desc_t),
    .message_size = 21,
    .object_size = sizeof(test_msg_t)
};

const test_msg_t TEST_MSG_INIT = {
    .descriptor = &TEST_MSG_DESCRIPTOR,
};

const field_desc_t TEST_MSG2_FIELDS_DESCRIPTOR[] = {
    {
        .element_size = sizeof(uint16_t),
        .element_count = 1,
        .inner_count = 1,
        .offset = offsetof(test_msg2_t, id),
        .stride = sizeof(uint16_t)
    },
    {
        .element_size = sizeof(double),
        .element_count = 1,
        .inner_count = 1,
        .offset = offsetof(test_msg2_t, sensor_blue) + offsetof(sensor_blue_t, r1),
        .stride = sizeof(sensor_blue_t)
    },
    {
        .element_size = sizeof(double),
        .element_count = 1,
        .inner_count = 1,
        .offset = offsetof(test_msg2_t, sensor_blue) + offsetof(sensor_blue_t, r2),
        .stride = sizeof(sensor_blue_t)
    },
};

const object_desc_t TEST_MSG2_DESCRIPTOR = {
    .fields = TEST_MSG2_FIELDS_DESCRIPTOR,
    .field_count = sizeof(TEST_MSG2_FIELDS_DESCRIPTOR) / sizeof(field_desc_t),
    .message_size = 18,
    .object_size = sizeof(test_msg2_t)
};

const test_msg2_t TEST_MSG2_INIT = {
    .descriptor = &TEST_MSG2_DESCRIPTOR,
};

