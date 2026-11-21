#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <stdint.h>
#include "Network.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Transport layer initialization function 
 * 
 * @param node Pointer to the local node configuration
 * 
 * @retval RC_OK    Initialization successful
 * @retval RC_FATAL Initialization failed due to invalid node configuration or failure in initializing internal components
 */
return_code_t Transport_Init(const halo_node_t* node);

/**
 * @brief Poll the transport layer to drive the TX and RX FSMs for a given channel and lane.
 * 
 * @param channel_key   The key of the channel to poll.
 * @param lane_id       The ID of the lane to poll.
 *
 * @retval  RC_OK            Polling completed successfully
 * @retval  RC_INVALID_ARG   Channel_key or lane_id are not valid
 *
 * @note    This function should be called periodically to allow the transport layer to process ongoing 
 *          transmissions and receptions.
 * 
 * @note    The return code indicates the success/failure of the polling operation, not of the transmission/reception.
 */
return_code_t Transport_Poll(uint8_t channel_key, uint8_t lane_id);

/**
 * @brief Reset the RX FSM for a given channel and lane.
 * 
 * @param channel_key   The key of the channel to reset.
 * @param lane_id       The ID of the lane to reset.
 * 
 * @retval  RC_OK            RX FSM reset successfully.
 * @retval  RC_INVALID_ARG   Channel_key or lane_id are not valid.
 */
return_code_t Transport_Rx_Reset(uint8_t channel_key, uint8_t lane_id);

/**
 * @brief Reset the TX FSM for a given channel and lane.
 * 
 * @param channel_key   The key of the channel to reset.
 * @param lane_id       The ID of the lane to reset.
 * 
 * @retval  RC_OK            TX FSM reset successfully.
 * @retval  RC_INVALID_ARG   Channel_key or lane_id are not valid.
 */
return_code_t Transport_Tx_Reset(uint8_t channel_key, uint8_t lane_id);

/**
 * @brief Sets the three-bit tag used on every segment of the next message.
 *
 * Set the tag before transmission starts. It may be changed after buffering
 * while the TX FSM is not busy, allowing a queued response to inherit its
 * request's tag.
 *
 * @retval RC_OK            Tag was set.
 * @retval RC_BUSY          A message is being transmitted.
 * @retval RC_INVALID_ARG   Invalid channel/lane or tag greater than 7.
 */
return_code_t Transport_Tx_Set_Tag(uint8_t channel_key, uint8_t lane_id, uint8_t tag);

/**
 * @brief Consumes the tag associated with the next fully received message.
 *
 * @retval RC_OK            Tag was returned.
 * @retval RC_OK_NO_DATA    No complete message tag is available.
 * @retval RC_INVALID_ARG   Invalid channel/lane or NULL tag pointer.
 */
return_code_t Transport_Rx_Get_Tag(uint8_t channel_key, uint8_t lane_id, uint8_t *tag_out);


/**
 * @brief Buffers a message for transmission on a given channel and lane.
 * 
 * @param channel_key The key of the channel to buffer the message for (index into the node's channels array).
 * @param lane_id The ID of the lane to buffer the message for (0 or 1).
 * @param msg Pointer to the message data to be buffered. The message should already be populated
 * 
 * @retval  RC_OK            Message was successfully buffered for transmission.
 * @retval  RC_BUSY          The specified channel and lane are currently busy serializing another message.
 * @retval  RC_INVALID_ARG   The specified channel_key or lane_id are not valid or the msg pointer is NULL.
 */
return_code_t Transport_Buffer_Message(uint8_t channel_key, uint8_t lane_id, void *msg);

/**
 * @brief Sends a message on a given channel and lane.
 * 
 * @param channel_key The key of the channel to send the message on.
 * @param lane_id The ID of the lane to send the message on.
 * 
 * @retval  RC_OK            Message transmission started successfully.
 * @retval  RC_OK_NO_DATA    No message has been buffered for transmission on the specified channel and lane.
 * @retval  RC_BUSY          Channel/lane is currently busy with another transmission
 * @retval  RC_INVALID_ARG   Channel_key/lane_id is not valid.
 */
return_code_t Transport_Send(uint8_t channel_key, uint8_t lane_id);

/**
 * @brief Receive a message on a given channel and lane.
 * 
 * @param channel_key   The key of the channel to receive the message from.
 * @param lane_id       The ID of the lane to receive the message from.
 * @param msg           Pointer to the buffer where the received message should be copied.
 * 
 * @retval  RC_OK            A message is available and was copied to the provided buffer successfully.
 * @retval  RC_OK_NO_DATA    No full message is currently available.
 * @retval  RC_INVALID_ARG   Channel_key or lane_id is invalid or msg pointer is NULL.
 */
return_code_t Transport_Receive(uint8_t channel_key, uint8_t lane_id, void* msg);

/**
 * @brief Receive a message on a given channel and lane, together with its metadata.
 *
 * Same behaviour as Transport_Receive (copies the message and consumes it), but also
 * returns the timestamp and the tag of THAT message. Both are captured by the Transport
 * when the message is completed, so they always belong to the message returned.
 *
 * @param channel_key       The key of the channel to receive the message from.
 * @param lane_id           The ID of the lane to receive the message from.
 * @param msg               Pointer to the buffer where the received message should be copied.
 * @param rx_timestamp_us   Output: local arrival time of the LAST frame of the message
 *                          (microseconds, 32-bit, wrap-safe). May be NULL.
 * @param tag_out           Output: three-bit tag of the message (0-7). May be NULL.
 *
 * @retval  RC_OK            A message is available and was copied, metadata populated.
 * @retval  RC_OK_NO_DATA    No full message is currently available.
 * @retval  RC_INVALID_ARG   Channel_key or lane_id is invalid or msg pointer is NULL.
 */
return_code_t Transport_Receive_With_Meta(uint8_t channel_key, uint8_t lane_id, void *msg,
                                          uint32_t *rx_timestamp_us, uint8_t *tag_out);



/**
 * @brief Check if a message has been buffered for transmission on a given channel and lane.
 * 
 * @param channel_key   The key of the channel to query.
 * @param lane_id       The ID of the lane to query.
 * 
 * @retval  RC_OK_TRUE      A message has been buffered for transmission on the specified channel and lane. 
 * @retval  RC_OK_FALSE     No message is currently buffered for transmission on the specified channel and lane.
 * @retval  RC_INVALID_ARG  The specified channel_key or lane_id are not valid.
 */
return_code_t Transport_Tx_Is_Msg_Buffered(uint8_t channel_key, uint8_t lane_id);

/**
 * @brief Check if the last transmission on a given channel and lane was aborted due to an error.
 * 
 * @param channel_key   The key of the channel to query.
 * @param lane_id       The ID of the lane to query.
 * 
 * @retval  RC_OK_TRUE      The last transmission on the specified channel and lane was aborted due to an error.
 * @retval  RC_OK_FALSE     The last transmission on the specified channel and lane was not aborted 
 *                          (either successful or still in progress).
 * 
 * @retval  RC_INVALID_ARG  The specified channel_key or lane_id are not valid.
 * 
 * @note    This function clears the abort event flag after being called, so subsequent calls will return false 
 *          until another abort event occurs.
 */
return_code_t Transport_Tx_Is_Abort(uint8_t channel_key, uint8_t lane_id);

/**
 * @brief Check if the last transmission on a given channel and lane has completed successfully.
 * 
 * @param channel_key   The key of the channel to query.
 * @param lane_id       The ID of the lane to query.
 * 
 * @retval  RC_OK_TRUE      The last transmission on the specified channel and lane completed successfully.
 * @retval  RC_OK_FALSE     The last transmission on the specified channel and lane is still in progress or failed.
 * @retval  RC_INVALID_ARG  Channel_key or lane_id is invalid.
 * 
 * @note    This function should be used in conjunction with Transport_Tx_Is_Abort to determine the final outcome of a transmission.
 *          This function clears the done event flag after being called, so subsequent calls will return false until another 
 *          transmission completes.
 */
return_code_t Transport_Tx_Is_Done(uint8_t channel_key, uint8_t lane_id);

/**
 * @brief Check if the specified channel and lane are currently busy with an ongoing transmission.
 * 
 * @param channel_key   The key of the channel to query.
 * @param lane_id       The ID of the lane to query.
 * 
 * @retval  RC_OK_TRUE      The specified channel and lane are currently busy with an ongoing transmission.
 * @retval  RC_OK_FALSE     The specified channel and lane are not currently busy and are ready for a new transmission.
 * @retval  RC_INVALID_ARG  The specified channel_key or lane_id are not valid.
 */
return_code_t Transport_Tx_Is_Busy(uint8_t channel_key, uint8_t lane_id);

/**
 * @brief Get the current status of the TX FSM on a given channel and lane.
 * 
 * @param channel_key   The key of the channel to query.
 * @param lane_id       The ID of the lane to query.
 * 
 * @retval  RC_OK       TX ready for new transmission
 * @retval  RC_BUSY     A transmission is in progress.
 * @retval  RC_FATAL    The FSM is in the failed state (either in the recovery phase or unrecoverable failure).
 * @retval  RC_INVALID_ARG   Channel_key or lane_id is invalid.
 */
return_code_t Transport_Tx_Get_Status(uint8_t channel_key, uint8_t lane_id);

/**
 * @brief Get the last error code of the transmission on a given channel and lane.
 * 
 * @param channel_key   The key of the channel to query.
 * @param lane_id       The ID of the lane to query.
 * 
 * @return Check return_code_t enum for possible error codes
 */
return_code_t Transport_Tx_Get_Error(uint8_t channel_key, uint8_t lane_id);



/**
 * @brief Check if a complete message is available for reception on a given channel and lane.
 * 
 * @param channel_key   The key of the channel to query.
 * @param lane_id       The ID of the lane to query.
 * 
 * @retval  RC_OK_TRUE      A complete message is available for reception.
 * @retval  RC_OK_FALSE     No complete message is currently available.
 * @retval  RC_INVALID_ARG  Channel_key or lane_id is invalid.
 */
return_code_t Transport_Rx_Is_Msg_Available(uint8_t channel_key, uint8_t lane_id);

/**
 * @brief Check if an overrun error occurred during the last reception on a given channel and lane.
 * 
 * @param channel_key   The key of the channel to query.
 * @param lane_id       The ID of the lane to query.
 * 
 * @retval  RC_OK_TRUE      An overrun error occurred during the last reception on the specified channel and lane.
 * @retval  RC_OK_FALSE     No overrun error occurred during the last reception on the specified channel and lane.
 * @retval  RC_INVALID_ARG  Channel_key or lane_id is invalid.
 * 
 * @note    This function clears the overrun event flag after being called, so subsequent calls will return false 
 *          until another overrun event occurs.
 */
return_code_t Transport_Rx_Is_Overrun(uint8_t channel_key, uint8_t lane_id);

/**
 * @brief Check if the last reception on a given channel and lane was aborted due to an error.
 * 
 * @param channel_key   The key of the channel to query.
 * @param lane_id       The ID of the lane to query.
 * 
 * @retval  RC_OK_TRUE      The last reception on the specified channel and lane was aborted due to an error.
 * @retval  RC_OK_FALSE     The last reception on the specified channel and lane was not aborted 
 *                          (either successful or still in progress).
 * @retval  RC_INVALID_ARG  The specified channel_key or lane_id are not valid.
 * 
 * @note    This function clears the abort event flag after being called, so subsequent calls will return false 
 *          until another abort event occurs.
 */
return_code_t Transport_Rx_Is_Abort(uint8_t channel_key, uint8_t lane_id);

/**
 * @brief Check if the specified channel and lane are currently busy with an ongoing reception.
 * 
 * @param channel_key   The key of the channel to query.
 * @param lane_id       The ID of the lane to query.
 * 
 * @retval  RC_OK_TRUE      The specified channel and lane are currently busy with an ongoing reception.
 * @retval  RC_OK_FALSE     The specified channel and lane are not currently busy and are ready to receive a new message.
 * @retval  RC_INVALID_ARG  The specified channel_key or lane_id are not valid.
 */
return_code_t Transport_Rx_Is_Busy(uint8_t channel_key, uint8_t lane_id);

/**
 * @brief Get the current status of RX FSM on a given channel and lane.
 * 
 * @param channel_key   The key of the channel to query.
 * @param lane_id       The ID of the lane to query.
 * 
 * @retval  RC_OK       RX ready for new reception
 * @retval  RC_BUSY     A reception is still in progress.
 * @retval  RC_FATAL    RX FSM is in the failed state (either in the recovery phase or unrecoverable failure).
 * @retval  RC_INVALID_ARG   Channel_key or lane_id is invalid.
 */
return_code_t Transport_Rx_Get_Status(uint8_t channel_key, uint8_t lane_id);

/**
 * @brief Get the last error code of the reception on a given channel and lane.
 * 
 * @param channel_key   The key of the channel to query.
 * @param lane_id       The ID of the lane to query.
 * 
 * @retval  Check return_code_t enum for possible error codes
 */
return_code_t Transport_Rx_Get_Error(uint8_t channel_key, uint8_t lane_id);


#ifdef __cplusplus
}
#endif

#endif // TRANSPORT_H