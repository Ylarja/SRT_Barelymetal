
#include "../Include/Ring_Buffer.h"

return_code_t Ring_Buffer_Init(ring_buffer_t *rb, void *storage, uint8_t capacity, uint8_t element_size) {

	if (rb == NULL || storage == NULL) {
		return RC_INVALID_ARG;
	}

	if (capacity == 0 || element_size == 0 || capacity > 256) {
		return RC_DATA_RANGE;
	}

	if ((capacity & (capacity - 1)) != 0) {
		// Capacity is not a power of two
		return RC_DATA_RANGE;
	}

	rb->buffer = (uint8_t *)storage;
	rb->capacity = capacity;
	rb->element_size = element_size;
	rb->head = 0;
	rb->tail = 0;

	return RC_OK;
}

return_code_t Ring_Buffer_Is_Empty(const ring_buffer_t *rb) {

	if (rb == NULL) {
		return RC_INVALID_ARG;
	}

	if (rb->head == rb->tail) {
		return RC_OK_TRUE;
	} else {
		return RC_OK_FALSE;
	}
}

return_code_t Ring_Buffer_Is_Full(const ring_buffer_t *rb) {
	if (rb == NULL) {
		return RC_INVALID_ARG;
	}

	if (((rb->head + 1) & (rb->capacity - 1)) == rb->tail) {
		return RC_OK_TRUE;
	} else {
		return RC_OK_FALSE;
	}
}

/* Producer */
return_code_t Ring_Buffer_Push(ring_buffer_t *rb, const void *element) {

	if (rb == NULL || element == NULL) {
		return RC_INVALID_ARG;
	}

	uint16_t next = (rb->head + 1) & (rb->capacity - 1);

	if (next == rb->tail) {
		return RC_NO_RESOURCE; // full
	}

	const uint8_t *src = (const uint8_t *)element;
	uint8_t *dst = rb->buffer + (rb->head * rb->element_size);

	for (uint8_t i = 0; i < rb->element_size; i++) {
		dst[i] = src[i];
	}

	rb->head = next;

	return RC_OK;
}

return_code_t Ring_Buffer_Push_N_Items(ring_buffer_t *rb, const void *elements, uint8_t n) {

	if (rb == NULL || elements == NULL) {
		return RC_INVALID_ARG;
	}

	if (n == 0) {
		return RC_DATA_RANGE;
	}

	uint8_t stored_count;
	Ring_Buffer_Stored_Count(rb, &stored_count);

	if (stored_count + n > rb->capacity - 1) {
		return RC_NO_RESOURCE; // not enough space
	}

	for (uint8_t j = 0; j < n; j++) {
		const uint8_t *src = (const uint8_t *)elements + (j * rb->element_size);
		uint8_t *dst = rb->buffer + (rb->head * rb->element_size);

		for (uint8_t i = 0; i < rb->element_size; i++) {
			dst[i] = src[i];
		}

		rb->head = (rb->head + 1) & (rb->capacity - 1);
	}

	return RC_OK;
}

return_code_t Ring_Buffer_Pop(ring_buffer_t *rb, void *element) {

	if (rb == NULL || element == NULL) {
		return RC_INVALID_ARG;
	}

	if (rb->tail == rb->head) {
		return RC_OK_NO_DATA; // empty
	}

	const uint8_t *src = rb->buffer + (rb->tail * rb->element_size);
	uint8_t *dst = (uint8_t *)element;

	for (uint8_t i = 0; i < rb->element_size; i++) {
		dst[i] = src[i];
	}

	rb->tail = (rb->tail + 1) & (rb->capacity - 1);

	return RC_OK;
}

return_code_t Ring_Buffer_Pop_N_Items(ring_buffer_t *rb, void *elements, uint8_t n) {

	if (rb == NULL || elements == NULL) {
		return RC_INVALID_ARG;
	}

	if (n == 0) {
		return RC_DATA_RANGE;
	}

	uint8_t stored_count;
	Ring_Buffer_Stored_Count(rb, &stored_count);

	if (stored_count < n) {
		return RC_OK_NO_DATA; // not enough data
	}

	for (uint8_t j = 0; j < n; j++) {
		const uint8_t *src = rb->buffer + (rb->tail * rb->element_size);
		uint8_t *dst = (uint8_t *)elements + (j * rb->element_size);

		for (uint8_t i = 0; i < rb->element_size; i++) {
			dst[i] = src[i];
		}

		rb->tail = (rb->tail + 1) & (rb->capacity - 1);
	}

	return RC_OK;
}

return_code_t Ring_Buffer_Discard(ring_buffer_t *rb, uint8_t n) {

	if (rb == NULL) {
		return RC_INVALID_ARG;
	}

	if (n == 0) {
		return RC_DATA_RANGE;
	}

	uint8_t stored_count;
	Ring_Buffer_Stored_Count(rb, &stored_count);

	if (stored_count < n) {
		return RC_OK_NO_DATA; // not enough data
	}

	rb->tail = (rb->tail + n) & (rb->capacity - 1);
	return RC_OK;
}

return_code_t Ring_Buffer_Clear(ring_buffer_t *rb) {
	if (rb == NULL) {
		return RC_INVALID_ARG;
	}
	rb->head = 0;
	rb->tail = 0;
	return RC_OK;
}

return_code_t Ring_Buffer_Stored_Count(const ring_buffer_t *rb, uint8_t *stored_count) {

	if (rb == NULL || stored_count == NULL) {
		return RC_INVALID_ARG;
	}
	*stored_count = (rb->head - rb->tail) & (rb->capacity - 1);

	return RC_OK;
}

return_code_t Ring_Buffer_Peek_Nth(const ring_buffer_t *rb, uint8_t n, void *element) {

	if (rb == NULL || element == NULL) {
		return RC_INVALID_ARG;
	}

	if (n == 0 || n > rb->capacity-1) {
		return RC_DATA_RANGE;
	}

	uint8_t stored_count;
	Ring_Buffer_Stored_Count(rb, &stored_count);

	if (rb->tail == rb->head || n > stored_count)
		return RC_OK_NO_DATA; // not enough data

	uint8_t index = (rb->tail + n - 1) & (rb->capacity - 1);

	uint8_t *src = (rb->buffer + (index * rb->element_size));
	uint8_t *dst = (uint8_t *)element;

	for (uint8_t i = 0; i < rb->element_size; i++) {
		dst[i] = src[i];
	}

	return RC_OK;
}

return_code_t Ring_Buffer_Get_Capacity(const ring_buffer_t *rb, uint8_t *capacity) {
	if (rb == NULL || capacity == NULL) {
		return RC_INVALID_ARG;
	}
	*capacity = rb->capacity;
	return RC_OK;
}

return_code_t Ring_Buffer_Get_Element_Size(const ring_buffer_t *rb, uint8_t *element_size) {
	if (rb == NULL || element_size == NULL) {
		return RC_INVALID_ARG;
	}
	*element_size = rb->element_size;
	return RC_OK;
}