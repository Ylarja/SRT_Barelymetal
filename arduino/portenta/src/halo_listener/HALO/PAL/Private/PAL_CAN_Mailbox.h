#ifndef CAN_RING_BUFFER_H
#define CAN_RING_BUFFER_H

//#include "../Include/PAL.h"
#include "../../../Core/Core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct{
    uint16_t channel_id;
    uint8_t lane_id;
    size_t msg_size;
}pal_mailbox_config_t;

/**
* @brief Structure representing a CAN mailbox with its associated ring buffer and channel ID.
**/
typedef struct can_mailbox can_mailbox_t;

/**
 * @brief Callback type for sync frame reception (called from ISR context).
 * @param src_id     Controller node ID that sent the sync frame
 * @param seq        Sync sequence number (from PKT_ID field)
 * @param sync_type  Frame subtype: CAN_SYNC_TYPE_SYNC, CAN_SYNC_TYPE_FOLLOW_UP, etc.
 * @param timestamp  The timestamp relevant to this frame, microseconds, 32-bit wrap-safe:
 *                   - SYNC:      local arrival time (HAL RX-ISR timestamp)
 *                   - FOLLOW_UP: controller TX timestamp (from payload)
 **/
typedef void (*pal_sync_rx_callback_t)(uint8_t src_id, uint8_t seq, uint8_t sync_type, uint32_t timestamp);

/**
 * @brief Callback type for data frame reception (called from RX handler context).
 * @param src_id     Source node ID from the CAN identifier
 * @param dst_id     Destination node ID from the CAN identifier
 * @param local_ch   Local channel ID from the CAN identifier
 * @param lane       Lane ID from the CAN identifier
 * @param packet_id  Packet ID from the CAN identifier (0 for the first segment)
 * @param tag        Three-bit message tag from the CAN identifier
 * @param timestamp  Local frame-arrival timestamp in microseconds (32-bit, wrap-safe)
 **/
typedef void (*pal_data_rx_callback_t)(uint8_t src_id, uint8_t dst_id, uint8_t local_ch,
                                       uint8_t lane, uint8_t packet_id, uint8_t tag,
                                       uint32_t timestamp);

/**
 * @brief Maximum number of distinct external CAN IDs tracked per analysis window.
 *        Used only on listener nodes. Frames from further IDs are counted in an
 *        aggregate "untracked" counter.
 **/
#define PAL_UNKNOWN_ID_MAX 18

/**
 * @brief One external CAN ID seen in the current window.
 **/
typedef struct {
    uint32_t can_id;   // full 29-bit extended CAN identifier
    uint32_t frames;   // number of frames received with this ID in the current window
} pal_unknown_id_t;


/**
 * @brief Registers a callback to be invoked from the RX ISR when a sync frame is received.
 *        Only one callback can be registered at a time.
 **/
return_code_t PAL_CAN_Mailbox_Register_Sync_Callback(pal_sync_rx_callback_t cb);

/**
 * @brief Registers a callback to be invoked when a data frame is received,
 *        before the frame is pushed into its mailbox.
 *        The callback runs in RX handler context and should not block.
 *        Only one callback can be registered at a time.
 **/
return_code_t PAL_CAN_Mailbox_Register_Data_Callback(pal_data_rx_callback_t cb);


/**
 * @brief Initializes the CAN mailboxes with the specified channel IDs.
 * 
 * @param channel_id_list An array of channel IDs for the mailboxes.
 * @param num_channels The number of channel IDs in the list.
 * 
 * @retval RC_OK                operation successful
 * @retval RC_INVALID_ARG       channel_id_list is NULL or num_channels is 0
 * @retval RC_FATAL             initialization failed
 **/
return_code_t PAL_CAN_Mailbox_Init(uint16_t *channel_id_list, uint8_t num_channels);


/**
 * @brief Pushes a CAN message into the mailbox corresponding to its channel ID.
 * 
 * @param msg Pointer to the CAN message to be pushed.
 * @param rx_timestamp_us  Local arrival time of the frame in microseconds (32-bit,
 *                         wrap-safe), as returned by HAL_CAN_Get_RX_Timestamp()
 * 
 * @retval RC_OK                operation successful, message pushed
 * @retval RC_INVALID_ARG       operation failed, msg is NULL
 * @retval RC_NO_RESOURCE       operation failed, mailbox is full
 * @retval RC_FATAL             operation failed, internal ring buffer error
 * @retval RC_INTEGRITY_FAIL    operation failed, channel ID not found
 **/
return_code_t PAL_CAN_Mailbox_Push(const can_msg_t *msg,uint32_t rx_timestamp_us);


/**
 * @brief Pops a CAN message from the mailbox corresponding to the specified channel ID.
 * 
 * @param channel_id The channel ID of the mailbox to pop from.
 * @param lane_id The lane ID of the mailbox to pop from.
 * @param msg Pointer to store the popped CAN message.
 * @param rx_timestamp_us  Output: local arrival timestamp stored with the frame
 *                         (microseconds, 32-bit). May be NULL if not needed.
 *
 * 
 * @retval RC_OK                operation successful, message copied to msg
 * @retval RC_OK_NO_DATA           operation successful, mailbox is empty
 * @retval RC_INVALID_ARG       operation failed, msg is NULL
 * @retval RC_FATAL             operation failed, internal ring buffer error
 * @retval RC_INTEGRITY_FAIL    operation failed, channel ID not found
 **/
return_code_t PAL_CAN_Mailbox_Pop(uint16_t channel_id, uint8_t lane_id, can_msg_t *msg,uint32_t *rx_timestamp_us);


/**
 * @brief Clears all messages from the mailbox corresponding to the specified channel ID.
 * 
 * @param channel_id The channel ID of the mailbox to clear.
 * @param lane_id The lane ID of the mailbox to clear.
 * 
 * @retval RC_OK                operation successful, mailbox cleared
 * @retval RC_FATAL             operation failed, internal ring buffer error
 * @retval RC_INTEGRITY_FAIL    operation failed, channel ID not found
 **/
return_code_t PAL_CAN_Mailbox_Clear(uint16_t channel_id, uint8_t lane_id);


/**
 * @brief Checks if the mailbox corresponding to the specified channel ID is empty.
 * 
 * @param channel_id The channel ID of the mailbox to check.
 * @param lane_id The lane ID of the mailbox to check.
 * 
 * @retval RC_OK_TRUE           operation successful, mailbox is empty
 * @retval RC_OK_FALSE          operation successful, mailbox is not empty
 * @retval RC_FATAL             operation failed, internal ring buffer error
 * @retval RC_INTEGRITY_FAIL    operation failed, channel ID not found
 **/
return_code_t PAL_CAN_Mailbox_Is_Empty(uint16_t channel_id, uint8_t lane_id);


/**
 * @brief Checks if the mailbox corresponding to the specified channel ID is full.
 * 
 * @param channel_id The channel ID of the mailbox to check.
 * @param lane_id The lane ID of the mailbox to check.
 * 
 * @retval RC_OK_TRUE           operation successful, mailbox is full
 * @retval RC_OK_FALSE          operation successful, mailbox is not full
 * @retval RC_FATAL             operation failed, internal ring buffer error
 * @retval RC_INTEGRITY_FAIL    operation failed, channel ID not found
 **/
return_code_t PAL_CAN_Mailbox_Is_Full(uint16_t channel_id, uint8_t lane_id);


/**
 * @brief Peeks at the N-th CAN message in the mailbox corresponding to the specified channel ID without removing it.
 * 
 * @param channel_id The channel ID of the mailbox to peek into.
 * @param lane_id The lane ID of the mailbox to peek into.
 * @param msg Pointer to store the peeked CAN message.
 * @param n The index (1-based) of the message to peek at.
 * 
 * @retval RC_OK                operation successful, message copied to msg
 * @retval RC_OK_NO_DATA        operation successful, no data available at index n
 * @retval RC_INVALID_ARG       operation failed, msg is NULL
 * @retval RC_DATA_RANGE        operation failed, n is 0
 * @retval RC_FATAL             operation failed, internal ring buffer error
 * @retval RC_INTEGRITY_FAIL    operation failed, channel ID not found
 **/
return_code_t PAL_CAN_Mailbox_Peek_Nth(uint16_t channel_id, uint8_t lane_id, can_msg_t *msg, uint8_t n);

/**
 * @brief Dumps the contents of the mailbox corresponding to the specified channel ID to the virtual serial console.
 * 
 * @param channel_id The channel ID of the mailbox to dump.
 * @param lane_id The lane ID of the mailbox to dump.
 * 
 * @retval RC_OK                operation successful, mailbox dumped
 * @retval RC_FATAL             operation failed, internal ring buffer error
 * @retval RC_INTEGRITY_FAIL    operation failed, channel ID not found
 **/
return_code_t PAL_CAN_Mailbox_Dump(uint16_t channel_id, uint8_t lane_id);

/**
 * @brief Enables external-ID tracking (listener nodes only).
 *        A frame is external if it does not belong to any configured channel, or if it
 *        is a sync frame (SYNC_FRAME = 0) whose source is not a known controller.
 *        Until this function is called, nothing is recorded.
 *
 * @param known_sync_src  IDs of the controllers whose sync frames are legitimate.
 * @param n               Number of entries in known_sync_src (max 16).
 *
 * @retval RC_OK           Tracking enabled
 * @retval RC_INVALID_ARG  known_sync_src is NULL with n > 0, or n > 16
 **/
return_code_t PAL_CAN_Mailbox_Enable_Unknown_Tracking(const uint8_t *known_sync_src, uint8_t n);

/**
 * @brief Copies the external IDs recorded since the previous call, then clears the table.
 *        Meant to be called once at the end of each analysis window.
 *
 * @param out               Output array of at least `max` entries.
 * @param max               Capacity of `out`. Entries beyond it are added to *untracked_frames.
 * @param count             Output: number of entries written to `out`.
 * @param untracked_frames  Output: frames from IDs that did not fit in the table
 *                          (or in `out`) during the window.
 *
 * @retval RC_OK           Table copied and cleared
 * @retval RC_OK_NO_DATA   Tracking not enabled on this node (outputs set to 0)
 * @retval RC_INVALID_ARG  A pointer is NULL
 **/
return_code_t PAL_CAN_Mailbox_Take_Unknown_Ids(pal_unknown_id_t *out, uint8_t max,
                                               uint8_t *count, uint32_t *untracked_frames);


#ifdef __cplusplus
}   
#endif

#endif // CAN_RING_BUFFER_H