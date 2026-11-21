
#include "../Include/SerDes.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool big_endian_arch = false;
static bool initialized = false;

static inline void SerDes_Init() {
	union {
		uint16_t value;
		uint8_t bytes[2];
	} test;
	test.value = 0x0102;
	big_endian_arch = (test.bytes[0] == 0x01);
	initialized = true;
}

static inline return_code_t Serialize_Bytes(const void *value, uint8_t *buffer, size_t size) {

	if (value == NULL || buffer == NULL) {
		return RC_INVALID_ARG;
	}

	if (size == 0) {
		return RC_INVALID_ARG;
	}

	const uint8_t *bytes = (const uint8_t *)value;

	// Serialize enforcing network byte order (big-endian)
	for (size_t i = 0; i < size; i++) {
		if (big_endian_arch) {
			buffer[i] = bytes[i];
		} else {
			buffer[i] = bytes[size - 1 - i];
		}
	}

	return RC_OK;
}

static inline return_code_t Deserialize_Bytes(const uint8_t *buffer, void *value, size_t size) {

	if (buffer == NULL || value == NULL) {
		return RC_INVALID_ARG;
	}

	if (size == 0) {
		return RC_INVALID_ARG;
	}

	uint8_t *bytes = (uint8_t *)value;

	// Derialize enforcing network byte order (big-endian)
	for (size_t i = 0; i < size; i++) {
		if (big_endian_arch) {
			bytes[i] = buffer[i];
		} else {
			bytes[i] = buffer[size - 1 - i];
		}
	}

	return RC_OK;
}

return_code_t Serialize_Object(const void *obj, uint8_t *buffer, const object_desc_t *descriptor) {

	if (obj == NULL || buffer == NULL || descriptor == NULL) {
		return RC_INVALID_ARG;
	}

	if (!initialized) {
		SerDes_Init();
	}

	uint8_t *current_buffer_pos = buffer;
	return_code_t rc;

	for (size_t i = 0; i < descriptor->field_count; i++) {

		const field_desc_t *field = &descriptor->fields[i];
	
		const uint8_t *base_field_addr = (const uint8_t *)obj + field->offset; 

		// Loop required for array elements
		for (size_t j = 0; j < field->element_count / field->inner_count; j++) {

			for (size_t k = 0; k < field->inner_count; k++) {

				const uint8_t *element_addr = base_field_addr + (j * field->stride) + (k * field->element_size);
				rc = Serialize_Bytes((const void *)element_addr, current_buffer_pos, field->element_size);

				if (rc != RC_OK) {
					return rc;
				}

				current_buffer_pos += field->element_size;
			}
		}
	}

	return RC_OK;
}

return_code_t Deserialize_Object(const uint8_t *buffer, void *obj, const object_desc_t *descriptor) {

	if (obj == NULL || buffer == NULL || descriptor == NULL) {
		return RC_INVALID_ARG;
	}

	if (!initialized) {
		SerDes_Init();
	}

	const uint8_t *current_buffer_pos = buffer;
	return_code_t rc;

	for (size_t i = 0; i < descriptor->field_count; i++) {

		const field_desc_t *field = &descriptor->fields[i];
		uint8_t *base_field_addr = (uint8_t *)obj + field->offset;

		// Loop required for array elements
		for (size_t j = 0; j < field->element_count / field->inner_count; j++) {

			for (size_t k = 0; k < field->inner_count; k++) {

				uint8_t *element_addr = base_field_addr + (j * field->stride) + (k * field->element_size);
				rc = Deserialize_Bytes(current_buffer_pos, (void *)element_addr, field->element_size);

				if (rc != RC_OK) {
					return rc;
				}

				current_buffer_pos += field->element_size;
			}
		}
	}
	return RC_OK;
}
