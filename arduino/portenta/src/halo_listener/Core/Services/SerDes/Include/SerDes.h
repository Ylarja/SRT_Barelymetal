#ifndef SERDES_H
#define SERDES_H

#include "../../../DOL/Include/DOL.h"
#include "../../../Core.h"
#include "../../../Core_Config.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Serializes an object into a buffer based on descriptor of the object.
 *
 * @param obj           Pointer to the source object to serialize.
 * @param buffer        Destination buffer to hold the serialized object.
 * @param descriptor    Descriptor of the object layout and size.
 * 
 * @retval RC_OK            Object serialized successfully
 * @retval RC_INVALID_ARG   One or more arguments are NULL
 */
return_code_t Serialize_Object(const void *obj, uint8_t *buffer, const object_desc_t *descriptor);

/**
 * @brief Deserializes an object from a buffer based on descriptor of the object.
 *
 * @param buffer        Source buffer containing the serialized object.
 * @param obj           Pointer to the destination object to populate.
 * @param descriptor    Descriptor of the object layout and size.
 * 
 * @retval RC_OK            Object deserialized successfully
 * @retval RC_INVALID_ARG   One or more arguments are NULL
 */
return_code_t Deserialize_Object(const uint8_t *buffer, void *obj, const object_desc_t *descriptor);

#ifdef __cplusplus
}
#endif

#endif // SERDES_H