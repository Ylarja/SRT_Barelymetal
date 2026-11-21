#ifndef HAL_CAN_H
#define HAL_CAN_H

#include "../../Core.h"

#ifdef __cplusplus
extern "C" {
#endif


#define CAN_MAX_DLC 8 // Only CAN 2.0 standard supported (max 8 bytes data length)

typedef struct {
  uint32_t id;               // 29-bit identifier
  uint8_t dlc;               // Data length code (0–8 bytes)
  uint8_t data[CAN_MAX_DLC]; // Payload bytes
} can_msg_t;

// Callback function type for CAN RX interrupt
typedef void (*hal_can_rx_callback_t)();

// Callback type invoked from TX complete ISR with a software timestamp (us).
// Must be minimal — no I/O, no blocking calls.
typedef void (*hal_can_tx_sync_cb_t)(uint32_t timestamp_us);

/*********************************************************************
 * @brief    Sends a CAN message
 *
 * @param    msg                Pointer to the CAN message to send
 *
 * @retval   RC_OK              CAN message sent successfully
 * @retval   RC_INVALID_ARG     msg is NULL
 * @retval   RC_HW_FAULT        Hardware failure occurred
 *********************************************************************/
return_code_t HAL_CAN_Send(can_msg_t *msg);

/*****************************************************************************
 * @brief    Receives a CAN message.
 *
 * @param    msg                Pointer to the CAN message structure to fill
 *
 * @retval   RC_OK              CAN message received successfully
 * @retval   RC_OK_NO_DATA      No CAN message available
 * @retval   RC_INVALID_ARG     msg is NULL
 ***************************************************************************/
return_code_t HAL_CAN_Receive(can_msg_t *msg);

/******************************************************************************
 * @brief    Initializes the CAN hardware with specified parameters.
 *
 * @param    baudrate           CAN bus baudrate in bits per second
 * @param    hw_filter_id       Hardware filter ID
 * @param    hw_filter_mask     Hardware filter mask
 * @param    rx_int_enable      Enable RX interrupt if true
 *
 * @retval   RC_OK              CAN hardware initialized successfully
 * @retval   RC_INVALID_ARG     Baudrate is unsupported
 * @retval   RC_HW_FAULT        Hardware initialization failed
 * 
 * @note     Supported baudrates: 125000, 250000, 500000, 1000000
 *****************************************************************************/
return_code_t HAL_CAN_Init_Hardware(uint32_t baudrate, uint32_t hw_filter_id, uint32_t hw_filter_mask, bool rx_int_enable);

/******************************************************************************
 * @brief    Adds an additional hardware acceptance filter entry.
 *
 * @param    filter_id          Filter ID to match
 * @param    filter_mask        Mask defining which bits of filter_id must match
 *
 * @retval   RC_OK              Filter added successfully
 * @retval   RC_NO_RESOURCE     No available filter slots (MCP2515: max 1 additional filter)
 * @retval   RC_HW_FAULT        Hardware failure
 *****************************************************************************/
return_code_t HAL_CAN_Add_Filter(uint32_t filter_id, uint32_t filter_mask);

/******************************************************************************
 * @brief    Registers a callback function to be called on CAN message reception interrupt.
 *
 * @param    callback_func      Pointer to the callback function.
 *
 * @retval   RC_OK              Callback registered successfully
 * @retval   RC_INVALID_ARG     Pointer to callback is NULL
 *****************************************************************************/
return_code_t HAL_CAN_Register_Callback(hal_can_rx_callback_t callback_func);

/******************************************************************************
 * @brief    Returns the local timestamp (microseconds, 32-bit) captured in the
 *           true RX interrupt for the most recent receive event. Lets upper
 *           layers stamp a frame's arrival at interrupt time even when the
 *           frame itself is read later in task context (e.g. mbed defers the
 *           read out of the ISR). Used for sync-frame RX timestamping.
 *
 * @retval   Local arrival timestamp in microseconds (32-bit, wrap-safe).
 *****************************************************************************/
uint32_t HAL_CAN_Get_RX_Timestamp(void);

/******************************************************************************
 * @brief    Registers a callback to be invoked from the TX complete ISR when
 *           a sync-tagged frame finishes transmission. The callback receives a
 *           software timestamp captured at ISR entry. Must be minimal.
 *
 * @param    callback           Pointer to the TX sync callback function.
 *
 * @retval   RC_OK              Callback registered successfully
 * @retval   RC_INVALID_ARG     Pointer to callback is NULL
 *****************************************************************************/
return_code_t HAL_CAN_Register_TX_Sync_Callback(hal_can_tx_sync_cb_t callback);

/******************************************************************************
 * @brief    Sends a CAN frame tagged for TX-complete timestamping.
 *           The registered TX sync callback will be invoked when this
 *           specific frame finishes transmission.
 *           Identical to HAL_CAN_Send in framing; the tag is internal.
 *
 * @param    msg                Pointer to the CAN message to send
 *
 * @retval   RC_OK              Frame queued successfully
 * @retval   RC_INVALID_ARG     msg is NULL
 * @retval   RC_HW_FAULT        Hardware failure
 *****************************************************************************/
return_code_t HAL_CAN_Send_Sync(can_msg_t *msg);




#ifdef __cplusplus
}
#endif

#endif // HAL_CAN_H