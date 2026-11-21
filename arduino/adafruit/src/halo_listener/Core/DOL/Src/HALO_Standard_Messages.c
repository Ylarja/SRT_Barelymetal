#include "../Private/HALO_Standard_Messages.h"

const field_desc_t HALO_REQUEST_MESSAGE_FIELDS_DESCRIPTOR[] = {
    {
        .element_size = sizeof(uint64_t),
        .element_count = 1,
        .inner_count = 1,
        .offset = offsetof(halo_request_message_t, timestamp),
        .stride = sizeof(uint64_t)
    },
};

const object_desc_t HALO_REQUEST_MESSAGE_DESCRIPTOR = {
    .fields = HALO_REQUEST_MESSAGE_FIELDS_DESCRIPTOR,
    .field_count = sizeof(HALO_REQUEST_MESSAGE_FIELDS_DESCRIPTOR) / sizeof(field_desc_t),
    .message_size = 8,
    .object_size = sizeof(halo_request_message_t)
};

const halo_request_message_t HALO_REQUEST_MESSAGE_INIT = {
    .descriptor = &HALO_REQUEST_MESSAGE_DESCRIPTOR,
};

