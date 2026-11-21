#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include "../../../Core.h"

#ifdef __cplusplus
extern "C" {
#endif

/***************
 * @brief Ring Buffer Structure
 * @warning This structure must be treated as opaque by users.
 *          Always use the provided API functions to interact with the buffer.
 * ***************/

typedef struct {
  uint8_t *buffer;       //!< Pointer to user-provided storage
  uint8_t capacity;      //!< Capacity of buffer in elements (must be power of two, max 256)
  uint8_t element_size;  //!< Size of each element in bytes
  volatile uint8_t head; //!< Written by producer after writing data
  volatile uint8_t tail; //!< Written by consumer after reading data
}ring_buffer_t;


/**************************** 
 * @brief Initialize a ring buffer
 * 
 * @param rb                Pointer to the ring buffer instance
 * @param storage           Pointer to user-provided storage
 * @param capacity          Total capacity of the buffer in elements (must be power of two)
 * @param element_size      Size of each element in bytes
 * 
 * @retval RC_OK                 Initialization successful
 * @retval RC_INVALID_ARG        rb or storage is NULL
 * @retval RC_DATA_RANGE         capacity is 0, element_size is 0, capacity is not a power of two, or capacity > 256
 ****************************/
return_code_t Ring_Buffer_Init(ring_buffer_t *rb, void *storage, uint8_t capacity, uint8_t element_size);

/**************************** 
 * @brief Check if the ring buffer is empty
 * 
 * @param rb                Pointer to the ring buffer instance
 * 
 * @retval RC_OK_TRUE          Buffer is empty
 * @retval RC_OK_FALSE         Buffer is not empty
 * @retval RC_INVALID_ARG      rb is NULL
 ****************************/
return_code_t Ring_Buffer_Is_Empty(const ring_buffer_t *rb);

/**************************** 
 * @brief Check if the ring buffer is full
 * 
 * @param rb                Pointer to the ring buffer instance
 * 
 * @retval RC_OK_TRUE          Buffer is full
 * @retval RC_OK_FALSE         Buffer is not full
 * @retval RC_INVALID_ARG      rb is NULL
 ****************************/
return_code_t Ring_Buffer_Is_Full(const ring_buffer_t *rb);

/**************************** 
 * @brief Push a single element into the ring buffer
 * 
 * @param rb                Pointer to the ring buffer instance
 * @param element           Pointer to the element to push
 * 
 * @retval RC_OK                 Element pushed successfully
 * @retval RC_INVALID_ARG        rb or element is NULL
 * @retval RC_NO_RESOURCE        Not enough space in the buffer
 ****************************/
return_code_t Ring_Buffer_Push(ring_buffer_t *rb, const void *element);

/**************************** 
 * @brief Push multiple elements into the ring buffer
 * 
 * @param rb                Pointer to the ring buffer instance
 * @param elements          Pointer to the elements to push
 * @param n                 Number of elements to push
 * 
 * @retval RC_OK                 Elements pushed successfully
 * @retval RC_INVALID_ARG        rb or elements is NULL
 * @retval RC_DATA_RANGE         n is 0
 * @retval RC_NO_RESOURCE        Not enough space in the buffer
 ****************************/
return_code_t Ring_Buffer_Push_N_Items(ring_buffer_t *rb, const void *elements, uint8_t n);

/**************************** 
 * @brief Pop a single element from the ring buffer
 * 
 * @param rb                Pointer to the ring buffer instance
 * @param element           Pointer to store the popped element
 * 
 * @retval RC_OK                 Element popped successfully
 * @retval RC_INVALID_ARG        rb or element is NULL
 * @retval RC_OK_NO_DATA         Not enough data in the buffer
 ****************************/
return_code_t Ring_Buffer_Pop(ring_buffer_t *rb, void *element);

/**************************** 
 * @brief Pop multiple elements from the ring buffer
 * 
 * @param rb                Pointer to the ring buffer instance
 * @param elements          Pointer to store the popped elements
 * @param n                 Number of elements to pop
 * 
 * @retval RC_OK                 Elements popped successfully
 * @retval RC_INVALID_ARG        rb or elements is NULL
 * @retval RC_DATA_RANGE         n is 0
 * @retval RC_OK_NO_DATA         Not enough data in the buffer
 ****************************/
return_code_t Ring_Buffer_Pop_N_Items(ring_buffer_t *rb, void *elements, uint8_t n);

/**************************** 
 * @brief Discard data from the ring buffer
 * 
 * @param rb                Pointer to the ring buffer instance
 * @param n                 Number of elements to discard
 * 
 * @retval RC_OK                 Data discarded successfully
 * @retval RC_INVALID_ARG        rb is NULL
 * @retval RC_DATA_RANGE         n is 0
 * @retval RC_OK_NO_DATA         Not enough data in the buffer
 ****************************/
return_code_t Ring_Buffer_Discard(ring_buffer_t *rb, uint8_t n);

/**************************** 
 * @brief Clear the ring buffer
 * 
 * @param rb                Pointer to the ring buffer instance
 * 
 * @retval RC_OK                 Buffer cleared successfully
 * @retval RC_INVALID_ARG        rb is NULL
 ****************************/
return_code_t Ring_Buffer_Clear(ring_buffer_t *rb);

/**************************** 
 * @brief Get the number of stored elements in the ring buffer
 * 
 * @param rb               Pointer to the ring buffer instance
 * @param stored_count     Pointer to store the number of stored elements
 * 
 * @retval RC_OK                 Operation successful
 * @retval RC_INVALID_ARG        rb or stored_count is NULL
 ****************************/
return_code_t Ring_Buffer_Stored_Count(const ring_buffer_t *rb, uint8_t *stored_count);

/**************************** 
 * @brief Peek at the Nth element in the ring buffer without removing it
 * 
 * @param rb                Pointer to the ring buffer instance
 * @param n                 Index of the element to peek at (1-based)
 * @param element           Pointer to store the peeked element
 * 
 * @retval RC_OK                 Element peeked successfully
 * @retval RC_INVALID_ARG        rb or element is NULL
 * @retval RC_DATA_RANGE         n is 0 or greater than capacity
 * @retval RC_OK_NO_DATA         no data available at index n
 ****************************/
return_code_t Ring_Buffer_Peek_Nth(const ring_buffer_t *rb, uint8_t n, void *element);

/**************************** 
 * @brief Get the capacity of the ring buffer in number of elements
 * 
 * @param rb                Pointer to the ring buffer instance
 * @param capacity          Pointer to store the capacity
 * 
 * @retval RC_OK                 Capacity retrieved successfully
 * @retval RC_INVALID_ARG        rb or capacity is NULL
 ****************************/
return_code_t Ring_Buffer_Get_Capacity(const ring_buffer_t *rb, uint8_t *capacity);

/**************************** 
 * @brief Get the element size of the ring buffer
 * 
 * @param rb               Pointer to the ring buffer instance
 * @param element_size     Pointer to store the element size
 * 
 * @retval RC_OK                 Element size retrieved successfully
 * @retval RC_INVALID_ARG        rb or element_size is NULL
 ****************************/
return_code_t Ring_Buffer_Get_Element_Size(const ring_buffer_t *rb, uint8_t *element_size);




#ifdef __cplusplus
}
#endif

#endif // RING_BUFFER_H