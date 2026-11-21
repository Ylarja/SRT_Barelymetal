#ifndef APPLICATION_H
#define APPLICATION_H

#include <stdint.h>
#include "Transport.h"


#ifdef __cplusplus
extern "C" {
#endif
typedef msg_desc_t halo_msg_desc_t;
extern const halo_channel_t HALO_CHANNEL_TEST1;
extern const halo_channel_t HALO_CHANNEL_TEST2;
extern const halo_node_t HALO_NODE_ADAFRUIT;

return_code_t Application_Init(const halo_node_t *halo_node);
return_code_t Application_Service(uint8_t steps);

return_code_t Controller_Send(void* msg, const halo_channel_t* channel);
return_code_t Controller_Request(const halo_channel_t* channel);
return_code_t Controller_Receive(const halo_channel_t* channel, void* msg);

return_code_t Responder_Receive(const halo_channel_t* channel, void* msg);



/**
 * @brief Application Layer API for responder to send response on lane 0
 * 
 * @param channel   Pointer to the struct representing the channel on which to send the response.
 * @param msg       Pointer to the message buffer containing the response data to be sent.
 * 
 * @retval RC_OK             Response was successfully sent.
 * @retval RC_BUSY           Slave is not ready to send a response (e.g., previous response is still being processed or transmitted).
 * @retval RC_PERM_DENIED    The specified node is not the responder for the specified channel
 * @retval RC_INTEGRITY_FAIL The specified channel is not valid or the channel key could not be derived from the channel ID
 * @retval RC_FATAL          An error occurred while trying to send the response.
 */
return_code_t Responder_Respond(const halo_channel_t* channel, void* msg);
/**
 * @brief Reports whether a response on a responder channel has finished transmitting
 *        since the previous call. The flag is consumed (cleared) by this call.
 *
 *        "Finished" means that every segment of the response has been handed to the
 *        CAN driver (Transport TX done).
 *
 * @note  The flag is set inside Application_Service() and is also cleared by
 *        Responder_Respond() when the responder FSM is idle. To count every response,
 *        call this function right after the service call and BEFORE Responder_Respond().
 *
 * @param channel   Responder channel to query
 *
 * @retval RC_OK_TRUE        A response completed since the previous call
 * @retval RC_OK_FALSE       No response completed
 * @retval RC_INVALID_ARG    Invalid channel pointer
 * @retval RC_PERM_DENIED    This node is not the responder for the channel
 * @retval RC_INTEGRITY_FAIL Channel not found in the node configuration
 */
return_code_t Responder_Respond_Is_Done(const halo_channel_t *channel);

/**
 * @brief Sends a sync burst to this node's responders. Call periodically on controller nodes.
 *        No-op (returns RC_OK) if the node has no controller endpoints.
 **/
return_code_t Controller_Sync(void);

/**
 * @brief Returns the time estimate in a controller's reference frame for a responder channel.
 *
 * @param channel   Channel whose controller defines the time domain
 * @param time_out  Output: estimated controller time in microseconds
 *
 * @retval RC_OK             Offset valid, time_out populated
 * @retval RC_OK_NO_DATA     No sync received yet from this controller
 * @retval RC_INTEGRITY_FAIL Channel invalid or node is not the responder
 **/
return_code_t Responder_Get_Controller_Time(const halo_channel_t *channel, uint32_t *time_out);

return_code_t Listener_Is_Msg_Available(const halo_channel_t *channel, uint8_t lane_id);
/**
 * @brief Retrieves the next complete message observed on a listener channel/lane,
 *        together with its timestamp and tag.
 *
 *        Lane 0: responder-to-controller responses. Lane 1: controller-to-responder messages.
 *        Message, timestamp and tag always belong to the same message.
 *
 * @param channel       Channel to query
 * @param lane_id       Lane to query (0 or 1)
 * @param msg           Output message; must be initialized with the INIT of the expected
 *                      type (the descriptor is in the first field)
 * @param timestamp_out Output: local arrival time of the LAST frame of the message
 *                      (microseconds, 32-bit, wrap-safe). May be NULL.
 * @param tag_out       Output: three-bit tag of the message (0-7). May be NULL.
 *                      On lane 1 it is always 0 with the current controller.
 *
 * @retval RC_OK             Message retrieved, outputs populated
 * @retval RC_OK_NO_DATA     No complete message is available
 * @retval RC_INVALID_ARG    Invalid lane
 * @retval RC_PERM_DENIED    The local node is not a listener for this channel
 * @retval RC_INTEGRITY_FAIL Invalid channel/context or msg does not match the lane descriptor
 * @retval RC_FATAL          The previous reception on this lane failed
 * @retval RC_ERROR          Transport error while copying the message
 */
return_code_t Listener_Receive(const halo_channel_t *channel, uint8_t lane_id, void *msg,
                               uint32_t *timestamp_out, uint8_t *tag_out);
/**
 * @brief Retrieves the next Lane 0 request's timestamp and CAN message tag.
 *
 * @param channel       Channel to query
 * @param timestamp_out Output local arrival timestamp in microseconds (32-bit, wrap-safe)
 * @param tag_out       Output three-bit CAN message tag (0-7)
 *
 * @retval RC_OK             Request event retrieved
 * @retval RC_OK_NO_DATA     No request event is queued
 * @retval RC_INVALID_ARG    An output pointer is NULL
 * @retval RC_PERM_DENIED    The local node is not a listener for this channel
 * @retval RC_INTEGRITY_FAIL Invalid channel or listener context
 */
return_code_t Listener_Receive_Request_With_Tag(const halo_channel_t *channel,
                                                uint32_t *timestamp_out,
                                                uint8_t *tag_out);
                                                
return_code_t Listener_Get_Controller_Time(const halo_channel_t *channel, uint32_t *time_out);

/**
 * @brief Returns the external CAN IDs seen since the previous call (with the number of
 *        frames each sent), then clears the table. Meant to be called once at the end of
 *        each analysis window.
 *
 *        External = extended frames that belong to no configured channel, or sync frames
 *        that do not come from a known controller. Available only on listener nodes
 *        (no controller or responder endpoints).
 *
 * @param out               Output array of at least `max` entries (PAL_UNKNOWN_ID_MAX is enough)
 * @param max               Capacity of `out`
 * @param count             Output: number of entries written to `out`
 * @param untracked_frames  Output: frames from IDs that did not fit in the table
 *
 * @retval RC_OK           Table copied and cleared
 * @retval RC_INVALID_ARG  A pointer is NULL
 * @retval RC_PERM_DENIED  This node is not a pure listener
 * @retval RC_OK_NO_DATA   Tracking not active (should not happen on a pure listener)
 */
return_code_t Listener_Take_Unknown_Ids(pal_unknown_id_t *out, uint8_t max,
                                        uint8_t *count, uint32_t *untracked_frames);

#ifdef __cplusplus
}
#endif

#endif // APPLICATION_H