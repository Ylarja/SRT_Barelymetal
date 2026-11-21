#ifndef LIFO_BUFFER_H
#define LIFO_BUFFER_H

#include "../../../Core.h"

#ifdef __cplusplus
extern "C" {
#endif


/***********************
* @brief LIFO Buffer Structure
* @warning This structure must be treated as opaque by users.
*          Always use the provided API functions to interact with the buffer.
**************************/
typedef struct {
    void    *buffer;            //!< Pointer to user-provided storage
    uint8_t capacity;           //!< Capacity of the buffer in elements
    uint8_t head;               //!< Index of the start of valid data
    uint8_t element_size;       //!< Size of each element in bytes
} lifo_buffer_t;


/**************************** 
 * @brief Initialize a LIFO buffer
 * 
 * @param buf               Pointer to the LIFO buffer instance
 * @param storage           Pointer to user-provided storage
 * @param capacity          Total capacity of the buffer in elements
 * @param element_size      Size of each element in bytes
 * 
 * @retval RC_OK                 Initialization successful
 * @retval RC_INVALID_ARG        buf or storage is NULL
 * @retval RC_DATA_RANGE         capacity is 0, element_size is 0, or capacity is not a multiple of element_size
 ****************************/
return_code_t LIFO_Buffer_Init(lifo_buffer_t *buf, void *storage, uint8_t capacity, uint8_t element_size);

/**************************** 
 * @brief Check if the LIFO buffer is empty
 * 
 * @param buf               Pointer to the LIFO buffer instance
 * 
 * @retval RC_OK_TRUE          Buffer is empty
 * @retval RC_OK_FALSE         Buffer is not empty
 * @retval RC_INVALID_ARG      buf is NULL
 ****************************/
return_code_t LIFO_Buffer_Is_Empty(const lifo_buffer_t *buf);

/**************************** 
 * @brief Check if the LIFO buffer is full
 * 
 * @param buf               Pointer to the LIFO buffer instance
 * 
 * @retval RC_OK_TRUE          Buffer is full
 * @retval RC_OK_FALSE         Buffer is not full
 * @retval RC_INVALID_ARG      buf is NULL
 ****************************/
return_code_t LIFO_Buffer_Is_Full(const lifo_buffer_t *buf);

/**************************** 
 * @brief Push a single item onto the LIFO buffer
 * 
 * @param buf               Pointer to the LIFO buffer instance
 * @param element           Pointer to the element to push
 * 
 * @retval RC_OK                 Element pushed successfully
 * @retval RC_NO_RESOURCE        Not enough space in the buffer
 * @retval RC_INVALID_ARG        buf or element is NULL
 ****************************/
return_code_t LIFO_Buffer_Push(lifo_buffer_t *buf, const void *element);

/**************************** 
 * @brief Push multiple elements onto the LIFO buffer
 * 
 * @param buf               Pointer to the LIFO buffer instance
 * @param elements          Pointer to the elements to push
 * @param n                 Number of elements to push
 * 
 * @retval RC_OK                 Elements pushed successfully
 * @retval RC_INVALID_ARG        buf or elements is NULL
 * @retval RC_DATA_RANGE         n is 0
 * @retval RC_NO_RESOURCE        Not enough space in the buffer
 ****************************/
return_code_t LIFO_Buffer_Push_N(lifo_buffer_t *buf, const void *elements, uint8_t n);

/**************************** 
 * @brief Pop a single item from the LIFO buffer
 * 
 * @param buf               Pointer to the LIFO buffer instance
 * @param element           Pointer to store the popped element
 * 
 * @retval RC_OK                 Element popped successfully
 * @retval RC_INVALID_ARG        buf or element is NULL
 * @retval RC_OK_NO_DATA         Not enough data in the buffer
 ****************************/
return_code_t LIFO_Buffer_Pop(lifo_buffer_t *buf, void *element);

/**************************** 
 * @brief Pop multiple elements from the LIFO buffer
 * 
 * @param buf               Pointer to the LIFO buffer instance
 * @param elements          Pointer to store the popped elements
 * @param n                 Number of elements to pop
 * 
 * @retval RC_OK                 Elements popped successfully
 * @retval RC_INVALID_ARG        buf or elements is NULL
 * @retval RC_DATA_RANGE         n is 0
 * @retval RC_OK_NO_DATA         Not enough data in the buffer
 ****************************/
return_code_t LIFO_Buffer_Pop_N(lifo_buffer_t *buf, void *elements, uint8_t n);

/**************************** 
 * @brief Discard data from the LIFO buffer
 * 
 * @param buf               Pointer to the LIFO buffer instance
 * @param n                 Number of elements to discard
 * 
 * @retval RC_OK                 Data discarded successfully
 * @retval RC_INVALID_ARG        buf is NULL
 * @retval RC_DATA_RANGE         n is 0
 * @retval RC_OK_NO_DATA         Not enough data in the buffer
 ****************************/
return_code_t LIFO_Buffer_Discard(lifo_buffer_t *buf, uint8_t n);

/**************************** 
 * @brief Clear the LIFO buffer
 * 
 * @param buf               Pointer to the LIFO buffer instance
 * 
 * @retval RC_OK                 Buffer cleared successfully
 * @retval RC_INVALID_ARG        buf is NULL
 ****************************/
return_code_t LIFO_Buffer_Clear(lifo_buffer_t *buf);

/**************************** 
 * @brief Get the capacity of the LIFO buffer in number of elements
 * 
 * @param buf               Pointer to the LIFO buffer instance
 * @param capacity          Pointer to store the capacity
 * 
 * @retval RC_OK                 Capacity retrieved successfully
 * @retval RC_INVALID_ARG        buf or capacity is NULL
 ****************************/
return_code_t LIFO_Buffer_Get_Capacity(lifo_buffer_t *buf, uint8_t* capacity);

/**************************** 
 * @brief Get the number of items stored in the LIFO buffer
 * 
 * @param buf               Pointer to the LIFO buffer instance
 * @param stored_count      Pointer to store the stored count
 * 
 * @retval RC_OK                 Stored count retrieved successfully
 * @retval RC_INVALID_ARG        buf or stored_count is NULL
 ****************************/
return_code_t LIFO_Buffer_Get_Stored_Count(lifo_buffer_t *buf, uint8_t* stored_count);

/**************************** 
 * @brief Get the element size of the LIFO buffer
 * 
 * @param buf               Pointer to the LIFO buffer instance
 * @param element_size     Pointer to store the element size
 * 
 * @retval RC_OK                 Element size retrieved successfully
 * @retval RC_INVALID_ARG        buf or element_size is NULL
 ****************************/
return_code_t LIFO_Buffer_Get_Element_Size(lifo_buffer_t *buf, uint8_t* element_size);

/**************************** 
 * @brief Peek at the N-th item in the LIFO buffer without removing it
 * 
 * @param buf               Pointer to the LIFO buffer instance
 * @param n                 Index of the item to peek at (0-based)
 * @param element           Pointer to store the peeked element
 * 
 * @retval RC_OK                 Element peeked successfully
 * @retval RC_INVALID_ARG        buf or element is NULL
 * @retval RC_DATA_RANGE         n is 0
 * @retval RC_OK_NO_DATA         Not enough data in the buffer
 ****************************/
return_code_t LIFO_Buffer_Peek_Nth(const lifo_buffer_t *buf, uint8_t n, void *element);


#ifdef __cplusplus
}
#endif

#endif // LIFO_BUFFER_H