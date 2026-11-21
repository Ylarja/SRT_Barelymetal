#ifndef NETWORK_H
#define NETWORK_H

#include <stdint.h>
#include "../../PAL/Include/PAL.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Network Initialization
 * 
 * @param node_desc Pointer to the local node description structure containing information about the node
 * 
 * @retval RC_OK        Network Layer initialization successful
 * @retval RC_FATAL     Network Layer initialization failed
 * 
 */
return_code_t Network_Init(const halo_node_t *node);

/**
 * @brief Retrieves the timestamp of the next observed Lane 0 request on a listener channel.
 *        Events are returned in arrival order and consumed by this call.
 *
 * @param channel_key   Index of the channel in the initialized node's channels array
 * @param timestamp_out Output timestamp in local microseconds (32-bit, wrap-safe)
 * @param tag_out       Output for the three-bit CAN message tag
 *
 * @retval RC_OK             An event was returned
 * @retval RC_OK_NO_DATA     No request event is queued
 * @retval RC_INVALID_ARG    Invalid channel key or NULL output pointer
 * @retval RC_PERM_DENIED    The local node is not a listener for the channel
 * @retval RC_INTEGRITY_FAIL Listener event queue was not initialized
 */
return_code_t Network_Get_Request_Event(uint8_t channel_key, uint32_t *timestamp_out,
                                        uint8_t *tag_out);

/**
 * @brief Network Polling Function
 *
 * @details Must be called from the main loop on every iteration.
 *          Handles background network layer work:
 *          - LED state management
 *          - FOLLOW_UP frame dispatch (deferred from TX ISR after Controller_Sync)
 */
void Network_Poll(void);

/**
 * @brief Network Send Function
 * 
 * @param pbuf          Pointer to the buffer containing the data to be sent and previous layers' headers
 * @param channel_key   Key identifying the channel to send on (index into node's channels array)
 * @param lane_id       ID of the lane to send on (0 or 1)
 * 
 * @retval RC_OK                Packet successfully sent    
 * @retval RC_ERROR             Failed to send packet due to a recoverable error
 * @retval RC_INTEGRITY_FAIL    Failed to send packet due to corrupted node/channel configuration
 * @retval RC_FATAL             Failed to send packet due to an unrecoverable error different from integrity failure
 * 
 */
return_code_t Network_Send(lifo_buffer_t *pbuf, uint8_t channel_key, uint8_t lane_id);

/**
 * @brief Network Receive Function
 * 
 * @param pbuf             Pointer to the buffer where received data and network header will be stored
 * @param channel_key      Key identifying the channel to receive on (index into node's channels array)
 * @param lane_id          ID of the lane to receive on (0 or 1)
 * @param rx_timestamp_us  Output: local arrival time of the received frame (us, 32-bit, wrap-safe).
 *                         May be NULL. Valid only when RC_OK is returned.
 * 
 * @retval RC_OK                Packet successfully received and stored in pbuf
 * @retval RC_OK_NO_DATA        No packet available to receive (or, on a listener, a frame of
 *                              the opposite direction was read and discarded)
 * @retval RC_INTEGRITY_FAIL    Failed to receive packet due to corrupted node/channel configuration or header integrity failure
 * @retval RC_FATAL             Failed to receive packet due to an unrecoverable error different from integrity failure
 * 
 */
return_code_t Network_Receive(lifo_buffer_t *pbuf, uint8_t channel_key, uint8_t lane_id,
                              uint32_t *rx_timestamp_us);

/**
 * @brief Check for Available Data on Network
 * 
 * @param channel_key   Key identifying the channel to check for data availability (index into node's channels array)
 * @param lane_id       ID of the lane to check for data availability (0 or 1)
 * 
 * @retval RC_OK_TRUE   Data is available to receive on the specified channel and lane
 * @retval RC_OK_FALSE  No data available to receive on the specified channel and lane
 * 
 */
return_code_t Network_Is_Data_Available(uint8_t channel_key, uint8_t lane_id);

/**
 * @brief Sends the SYNC frame of a sync burst. The matching FOLLOW_UP frame is
 *        dispatched automatically by Network_Poll() (via PAL_Service_Sync) once
 *        the SYNC transmit completes. Must only be called on controller nodes.
 *
 * @retval RC_OK    SYNC frame queued successfully
 * @retval RC_ERROR Frame failed to transmit
 */
return_code_t Network_Send_Sync(void);

/**
 * @brief Returns the estimated current time in the reference frame of the given controller.
 *
 * @param ctrl_id   Node ID of the controller whose time domain is requested
 * @param time_out  Output: estimated controller time in microseconds
 *
 * @retval RC_OK          Offset is valid and time_out has been populated
 * @retval RC_OK_NO_DATA  No sync has been received yet from this controller
 * @retval RC_INVALID_ARG time_out is NULL
 */
return_code_t Network_Get_Controller_Time(uint8_t ctrl_id, uint32_t *time_out);

#ifdef __cplusplus
}
#endif

#endif // NETWORK_H