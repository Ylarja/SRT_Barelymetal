#include "../Include/LIFO_Buffer.h"
#include <stdint.h>
#include <stdio.h>

return_code_t LIFO_Buffer_Init(lifo_buffer_t *buf, void *storage, uint8_t capacity, uint8_t element_size) {

  if (buf == NULL || storage == NULL) {
    return RC_INVALID_ARG;
  }

  if (capacity == 0 || element_size == 0 || capacity > 256) {
    return RC_DATA_RANGE;
  }

  buf->buffer = storage;
  buf->capacity = capacity;
  buf->head = 0;
  buf->element_size = element_size;
  return RC_OK;
}

return_code_t LIFO_Buffer_Is_Empty(const lifo_buffer_t *buf) {
  if (buf == NULL) {
    return RC_INVALID_ARG;
  }

  if (buf->head == 0) {
    return RC_OK_TRUE;
  } else {
    return RC_OK_FALSE;
  }
}

return_code_t LIFO_Buffer_Is_Full(const lifo_buffer_t *buf) {

  if (buf == NULL) {
    return RC_INVALID_ARG;
  }

  if (buf->head == buf->capacity) {
    return RC_OK_TRUE;
  } else {
    return RC_OK_FALSE;
  }
}

return_code_t LIFO_Buffer_Push(lifo_buffer_t *buf, const void *element) {

  if (buf == NULL || element == NULL) {
    return RC_INVALID_ARG;
  }

  uint16_t next = buf->head + 1;

  if (next > buf->capacity) {
    // Not enough space
    return RC_NO_RESOURCE;
  }

  const uint8_t *src = (const uint8_t *)element;
  uint8_t *dst = (uint8_t *)buf->buffer + (buf->head * buf->element_size);
  for (uint8_t i = 0; i < buf->element_size; i++) {
    dst[i] = src[i];
  }

  buf->head = next;

  return RC_OK;
}

return_code_t LIFO_Buffer_Push_N(lifo_buffer_t *buf, const void *elements, uint8_t n) {

  if (buf == NULL || elements == NULL) {
    return RC_INVALID_ARG;
  }

  if (n == 0) {
    return RC_DATA_RANGE;
  }

  uint16_t next = buf->head + n;

  if (next > buf->capacity) {
    // Not enough space
    return RC_NO_RESOURCE;
  }

  for (uint8_t j = 0; j < n; j++) {
    const uint8_t *src = (const uint8_t *)elements + (j * buf->element_size);
    uint8_t *dst = (uint8_t *)buf->buffer + ((buf->head + j) * buf->element_size);
    for (uint8_t i = 0; i < buf->element_size; i++) {
      dst[i] = src[i];
    }
  }

  buf->head = next;

  return RC_OK;
}

return_code_t LIFO_Buffer_Pop(lifo_buffer_t *buf, void *element) {

  if (buf == NULL || element == NULL) {
    return RC_INVALID_ARG;
  }

  if (buf->head < 1) {
    // Not enough data
    return RC_OK_NO_DATA;
  }

  buf->head--;

  const uint8_t *src = (const uint8_t *)buf->buffer + (buf->head * buf->element_size);
  uint8_t *dst = (uint8_t *)element;
  for (uint8_t i = 0; i < buf->element_size; i++) {
    dst[i] = src[i];
  }

  return RC_OK;
}

return_code_t LIFO_Buffer_Pop_N(lifo_buffer_t *buf, void *elements, uint8_t n) {

  if (buf == NULL || elements == NULL) {
    return RC_INVALID_ARG;
  }

  if (n == 0) {
    return RC_DATA_RANGE;
  }

  if (buf->head < n) {
    // Not enough data
    return RC_OK_NO_DATA;
  }

  for (uint8_t j = 0; j < n; j++) {
    const uint8_t *src = (uint8_t *)buf->buffer + ((buf->head - 1 - j) * buf->element_size);
    uint8_t *dst = (uint8_t *)elements + (j * buf->element_size);
    for (uint8_t i = 0; i < buf->element_size; i++) {

      dst[i] = src[i];
    }
  }

  buf->head -= n;
  return RC_OK;
}

return_code_t LIFO_Buffer_Discard(lifo_buffer_t *buf, uint8_t n) {

  if (buf == NULL) {
    return RC_INVALID_ARG;
  }

  if (n == 0) {
    return RC_DATA_RANGE;
  }

  if (buf->head < n) {
    // Not enough data
    return RC_OK_NO_DATA;
  }

  buf->head -= n;

  return RC_OK;
}

return_code_t LIFO_Buffer_Get_Capacity(lifo_buffer_t *buf, uint8_t *capacity) {
  if (buf == NULL || capacity == NULL) {
    return RC_INVALID_ARG;
  }
  *capacity = buf->capacity;
  return RC_OK;
}

return_code_t LIFO_Buffer_Get_Stored_Count(lifo_buffer_t *buf, uint8_t *stored_count) {
  if (buf == NULL || stored_count == NULL) {
    return RC_INVALID_ARG;
  }
  *stored_count = buf->head;
  return RC_OK;
}

return_code_t LIFO_Buffer_Get_Element_Size(lifo_buffer_t *buf, uint8_t *element_size) {
  if (buf == NULL || element_size == NULL) {
    return RC_INVALID_ARG;
  }
  *element_size = buf->element_size;
  return RC_OK;
}

return_code_t LIFO_Buffer_Clear(lifo_buffer_t *buf) {
  if (buf == NULL) {
    return RC_INVALID_ARG;
  }
  buf->head = 0;
  return RC_OK;
}

return_code_t LIFO_Buffer_Peek_Nth(const lifo_buffer_t *buf, uint8_t n, void *element) {

  if (buf == NULL || element == NULL) {
    return RC_INVALID_ARG;
  }

  if (n == 0 || n > buf->capacity) {
    return RC_DATA_RANGE;
  }

  if (buf->head < n) {
    // Not enough data
    return RC_OK_NO_DATA;
  }

  const uint32_t index = (buf->head - n) * buf->element_size;
  for (uint8_t i = 0; i < buf->element_size; i++) {
    ((uint8_t *)element)[i] = ((uint8_t *)buf->buffer)[index + i];
  }
  return RC_OK;
}