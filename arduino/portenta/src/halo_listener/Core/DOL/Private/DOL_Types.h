#ifndef DOL_TYPES_H
#define DOL_TYPES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const size_t      element_size; // Size in bytes of a single element (e.g., 2 for uint16_t)
    const size_t      element_count; // Number of elements in the array (e.g., 1 for scalar)
    const size_t      inner_count;  // Total number of elements in nested arrays (e.g., 6 for a 2x3 array)
    const size_t      offset;       // Byte offset of this field within the source struct
    const size_t      stride;       // Byte distance between consecutive elements
} field_desc_t;

typedef struct {
    const field_desc_t* fields;
    const size_t field_count;
    const size_t message_size; // size of the serialzied message
    const size_t object_size;  // size of the source object
} object_desc_t;

#ifdef __cplusplus
}
#endif

#endif // DOL_TYPES_H