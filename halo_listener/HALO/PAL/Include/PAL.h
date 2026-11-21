#ifndef PAL_H
#define PAL_H

#include "../../../Core/Core.h"

#include "../Private/PAL_Config.h"
#include "../Private/PAL_CAN_Mailbox.h"

#ifdef __cplusplus
extern "C" {
#endif



return_code_t PAL_Platform_Init(const halo_node_t *node_desc);
return_code_t PAL_Send(lifo_buffer_t *pbuf);
/**
 * @brief Pops the next frame of a channel/lane from its mailbox and unpacks it into pbuf.
 *
 * @param pbuf             Protocol buffer to fill (data + header fields).
 * @param channel_id       Global channel ID of the mailbox.
 * @param lane_id          Lane ID of the mailbox.
 * @param rx_timestamp_us  Output: local arrival time of the frame (us, 32-bit, wrap-safe).
 *                         May be NULL if not needed. Valid only when RC_OK is returned.
 *
 * @retval RC_OK           Frame unpacked into pbuf
 * @retval RC_OK_NO_DATA   Mailbox empty
 * @retval RC_FATAL        Mailbox error or channel/lane not found
 **/
return_code_t PAL_Receive(lifo_buffer_t *pbuf, uint16_t channel_id, uint8_t lane_id,
                          uint32_t *rx_timestamp_us);
return_code_t PAL_Is_Data_Available(uint16_t channel_id, uint8_t lane_id);

return_code_t PAL_Mailbox_Peek_Nth(lifo_buffer_t *pbuf, uint16_t channel_id, uint8_t lane_id, uint8_t n);
return_code_t PAL_Mailbox_Clear(uint16_t channel_id, uint8_t lane_id);
return_code_t PAL_Mailbox_Dump(uint16_t channel_id, uint8_t lane_id);

return_code_t PAL_Get_Max_Payload_Bytes(uint8_t *payload_size);

/**
 * @brief Sends the SYNC frame of a two-step sync burst on the CAN bus, bypassing the
 *        normal LIFO buffer pipeline. The FOLLOW_UP frame is sent later by
 *        PAL_Service_Sync() once the TX complete callback has fired.
 *
 * @param src_id  Source node ID (controller's own ID)
 * @param dst_id  Destination field (set to controller's own ID by convention)
 * @param seq     Sync sequence counter (7-bit, wraps at 128)
 **/
return_code_t PAL_Send_Sync(uint8_t src_id, uint8_t dst_id, uint8_t seq);

/**
 * @brief Must be called from task context (e.g. inside the application service loop).
 *        Sends the FOLLOW_UP frame if a TX complete callback has fired since the
 *        last PAL_Send_Sync call. No-op if no follow-up is pending.
 **/
return_code_t PAL_Service_Sync(void);

/**
 * @brief Registers a callback to be invoked from the RX ISR when a sync frame arrives.
 *        Wraps PAL_CAN_Mailbox_Register_Sync_Callback.
 **/
return_code_t PAL_Register_Sync_Callback(pal_sync_rx_callback_t cb);

/**
 * @brief Registers a callback to be invoked when a data frame is received,
 *        before it is pushed into its mailbox.
 *        Wraps PAL_CAN_Mailbox_Register_Data_Callback.
 **/
return_code_t PAL_Register_Data_Frame_Callback(pal_data_rx_callback_t cb);

/**
 * @brief Copies the external CAN IDs seen since the previous call, then clears the table.
 *        External = extended frames that belong to no configured channel, or sync frames
 *        that do not come from a known controller. Tracking is active only on listener
 *        nodes (no controller or responder endpoints); on other nodes RC_OK_NO_DATA is
 *        returned and the outputs are 0.
 *        Wraps PAL_CAN_Mailbox_Take_Unknown_Ids.
 *
 * @param out               Output array of at least `max` entries
 * @param max               Capacity of `out`
 * @param count             Output: number of entries written to `out`
 * @param untracked_frames  Output: frames from IDs that did not fit in the table
 *
 * @retval RC_OK           Table copied and cleared
 * @retval RC_OK_NO_DATA   Tracking not active on this node
 * @retval RC_INVALID_ARG  A pointer is NULL
 **/
return_code_t PAL_Take_Unknown_Ids(pal_unknown_id_t *out, uint8_t max,
                                   uint8_t *count, uint32_t *untracked_frames);

/**
 * @brief Registers the HAL TX sync callback so the PAL is notified when the
 *        SYNC frame transmission completes. Must be called during PAL init.
 *        Wraps HAL_CAN_Register_TX_Sync_Callback.
 **/
return_code_t PAL_Register_Sync_TX_Callback(void);


#ifdef __cplusplus
}
#endif

#endif // PAL_H