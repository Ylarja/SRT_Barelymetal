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
    uint64_t timestamp;
} halo_request_message_t;

extern const object_desc_t HALO_REQUEST_MESSAGE_DESCRIPTOR;

extern const halo_request_message_t HALO_REQUEST_MESSAGE_INIT;

#ifdef __cplusplus
}
#endif
