/**
 * @file PAL_CAN_Config.h
 * @brief Configuration parameters for the CAN-specific implementation of the PAL layer.
 */

#ifndef PAL_CAN_CONFIG_H
#define PAL_CAN_CONFIG_H

#include "../../Config/Include/Config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 
* @brief CAN Packaging Format
* 
* @details CAN Standard: CAN 2.0B (Extended ID)
*
*          CAN Extended ID (29 bits): used for message identification and routing.
*           - Source ID:          7 bits  (configurable, set by network tool)
*           - Destination ID:     7 bits  (configurable, set by network tool)
*           - Local Channel ID:   1 bit   (configurable, set by network tool)
*           - Lane ID:            1 bit
*           - Packet ID:          7 bits
*           - Retransmission ID:  2 bits  (repurposed as SYNC_TYPE for sync frames)
*           - SYNC_FRAME:         1 bit   (active-low: 0 = sync frame, 1 = data frame)
*           - TAG:                3 bits
*
*          SRC_ID + DST_ID + CH_ID always total 15 bits regardless of their individual widths.
*          The lower 14 bits (LANE through TAG) are therefore always at fixed positions.
*
*          SYNC_FRAME polarity:
*           0 = sync frame — dominant bit, wins CAN arbitration over data frames with same prefix
*           1 = data frame — recessive bit, normal unicast or multicast payload
*
*          Sync frame ID layout (SYNC_FRAME=0):
*           SRC=ctrl_id, DST=0x00 (broadcast), CH=0, LANE=0,
*          SYNC_FRAME=0, PKT_ID=sequence_number, RETRANS_ID=SYNC_TYPE, TAG=0
*
*          Packaging Structure:
*          | SRC_ID | DST_ID | CH_ID | LANE_ID | SYNC_FRAME | PKT_ID | RETRANS_ID | TAG      |
*          |  7bit  |  7bit  |  1bit |   1bit  |    1bit    |  7bit  |    2bit    |   3bit   |
*
*/


/**
 * @brief Default CAN Baudrate
 * @details Defines the default baudrate for CAN communication.
 **/
#define CAN_DEFAULT_BAUDRATE 1000000U

/**
 * @brief Default CAN ID Length
 * @details Defines the default CAN ID length.
 */
#define CAN_MSG_ID_LENGTH 29


/**
 * @brief CAN Identifier Bit Definitions
 * @details Defines the number of bits allocated for each field within the CAN extended identifier.
 **/
#define CAN_SRC_ID_BITS HALO_NETWORK_MODEL_NODE_ID_BITS
#define CAN_DST_ID_BITS HALO_NETWORK_MODEL_NODE_ID_BITS
#define CAN_CHANNEL_ID_BITS HALO_NETWORK_MODEL_LOCAL_CHANNEL_ID_BITS
#define CAN_LANE_ID_BITS 1
#define CAN_PACKET_ID_BITS 7
#define CAN_RETRANS_ID_BITS 2
#define CAN_SYNC_FRAME_BITS 1
#define CAN_RESERVED_BITS 3
#define CAN_TAG_BITS CAN_RESERVED_BITS


/**
 * @brief CAN Identifier Shifts
 * @details Defines the bit shifts for positioning various fields within the CAN extended identifier.
 **/
#define CAN_SRC_ID_SHIFT     (CAN_MSG_ID_LENGTH - CAN_SRC_ID_BITS)
#define CAN_DST_ID_SHIFT     (CAN_SRC_ID_SHIFT     - CAN_DST_ID_BITS)
#define CAN_CHANNEL_ID_SHIFT (CAN_DST_ID_SHIFT     - CAN_CHANNEL_ID_BITS)
#define CAN_LANE_ID_SHIFT    (CAN_CHANNEL_ID_SHIFT  - CAN_LANE_ID_BITS)
#define CAN_SYNC_FRAME_SHIFT (CAN_LANE_ID_SHIFT     - CAN_SYNC_FRAME_BITS)
#define CAN_PACKET_ID_SHIFT  (CAN_SYNC_FRAME_SHIFT  - CAN_PACKET_ID_BITS)
#define CAN_RETRANS_ID_SHIFT (CAN_PACKET_ID_SHIFT   - CAN_RETRANS_ID_BITS)
#define CAN_TAG_SHIFT        (CAN_RETRANS_ID_SHIFT  - CAN_TAG_BITS)

/**
 * @brief CAN Global Channel ID Shifts
 * @details Defines the bit shifts for positioning various fields  *          within the Global Channel ID.
 */
#define CAN_GLOBAL_CHANNEL_ID_LOCAL_CHANNEL_ID_SHIFT 0
#define CAN_GLOBAL_CHANNEL_ID_HIGH_ID_SHIFT  (CAN_GLOBAL_CHANNEL_ID_LOCAL_CHANNEL_ID_SHIFT + CAN_CHANNEL_ID_BITS)
#define CAN_GLOBAL_CHANNEL_ID_LOW_ID_SHIFT (CAN_GLOBAL_CHANNEL_ID_HIGH_ID_SHIFT + CAN_DST_ID_BITS)


/**
 * @brief Helper function for computing masks
 */
#define CAN_NBIT_MASK(n) ((uint32_t)((1UL << (n)) - 1UL))

/**
 * @brief CAN Identifier Masks
 * @details Defines the bit masks for extracting various fields from the CAN extended identifier.
 */
#define CAN_SRC_DST_ID_MASK CAN_NBIT_MASK(CAN_SRC_ID_BITS)
#define CAN_CHANNEL_ID_MASK CAN_NBIT_MASK(CAN_CHANNEL_ID_BITS)
#define CAN_LANE_ID_MASK 0x01
#define CAN_PACKET_ID_MASK 0x7F
#define CAN_RETRANS_ID_MASK 0x03
#define CAN_SYNC_FRAME_MASK 0x01
#define CAN_TAG_MASK CAN_NBIT_MASK(CAN_TAG_BITS)

/**
 * @brief Sync frame type, encoded in the RETRANS_ID field for SYNC_FRAME=0 frames.
 * RETRANS_ID is unused for sync frames (they are never retransmitted), so the
 * full 2-bit field is repurposed to distinguish sync frame subtypes.
 * The SYNC_FRAME bit is active-low: 0 = sync frame, 1 = data frame.
 * This ensures sync frames have a dominant bit at that position, giving them
 * higher CAN bus priority over data frames with identical SRC/DST/CH prefix.
 **/
#define CAN_SYNC_TYPE_SYNC       0x00U  // Initial sync frame, no payload
#define CAN_SYNC_TYPE_FOLLOW_UP  0x01U  // Follow-up: carries TX timestamp (8 bytes)
#define CAN_SYNC_TYPE_AUTH       0x02U  // Reserved: future per-pair authentication frame
#define CAN_SYNC_TYPE_RESERVED   0x03U

// DST_ID used in all sync frames. Value 0x00 acts as broadcast.
// The real discriminant is SYNC_FRAME=0 (active-low = sync frame), and DST_ID=0
// gives sync frames lower arbitration bits (higher CAN bus priority) than
// unicast data frames whose DST_ID is always a non-zero node ID.
#define CAN_SYNC_BROADCAST_DST_ID 0x00U


/**
 * @brief CAN Hardware Filter Mask
 * @details Normal data frames: accept if DST_ID=my_id AND SYNC_FRAME=1.
 *          SYNC_FRAME is active-low (0=sync, 1=data), so the filter_id must
 *          set SYNC_FRAME=1 and the mask must include it.
 **/
#define CAN_HARDWARE_FILTER_MASK ((CAN_NBIT_MASK(CAN_DST_ID_BITS) << CAN_DST_ID_SHIFT) | ((uint32_t)CAN_SYNC_FRAME_MASK << CAN_SYNC_FRAME_SHIFT))
/* Filtro del listener: qualunque DST_ID, solo frame dati (SYNC_FRAME=1) */
#define CAN_LISTENER_DATA_FILTER_MASK ((uint32_t)CAN_SYNC_FRAME_MASK << CAN_SYNC_FRAME_SHIFT)

/**
 * @brief CAN Sync Hardware Filter Mask
 * @details Sync frames: accept if SRC_ID=ctrl_id AND SYNC_FRAME=0.
 *          SYNC_FRAME is active-low so filter_id leaves that bit clear;
 *          the mask includes it so only SYNC_FRAME=0 frames pass.
 *          DST_ID=0 (broadcast) gives sync frames higher bus priority.
 **/
#define CAN_SYNC_FILTER_MASK ((CAN_NBIT_MASK(CAN_SRC_ID_BITS) << CAN_SRC_ID_SHIFT) | ((uint32_t)CAN_SYNC_FRAME_MASK << CAN_SYNC_FRAME_SHIFT))


/**
 * @brief Identifier Byte Definitions
 * @details Defines the number of bytes required to store each field separately within a buffer.
 */
#define CAN_SRC_ID_BYTES ((CAN_SRC_ID_BITS + 7)/8)
#define CAN_DST_ID_BYTES ((CAN_DST_ID_BITS + 7)/8)
#define CAN_CHANNEL_ID_BYTES ((CAN_CHANNEL_ID_BITS + 7)/8)
#define CAN_LANE_ID_BYTES ((CAN_LANE_ID_BITS + 7)/8)
#define CAN_PACKET_ID_BYTES ((CAN_PACKET_ID_BITS + 7)/8)
#define CAN_RETRANS_ID_BYTES ((CAN_RETRANS_ID_BITS + 7)/8)
#define CAN_RESERVED_BYTES ((CAN_RESERVED_BITS + 7)/8)
#define CAN_ID_TOTAL_BYTES (CAN_SRC_ID_BYTES + CAN_DST_ID_BYTES + CAN_CHANNEL_ID_BYTES + CAN_LANE_ID_BYTES + CAN_PACKET_ID_BYTES + CAN_RETRANS_ID_BYTES + CAN_RESERVED_BYTES)
#define CAN_DLC_BYTES 1
#define CAN_DATA_MAX_BYTES 8
#define CAN_MESSAGE_MAX_BYTES (CAN_ID_TOTAL_BYTES + CAN_DLC_BYTES + CAN_DATA_MAX_BYTES)

/**
 * @brief Compile-time assertions to validate configuration parameters.
 **/

 CONFIG_ASSERT(CAN_SRC_ID_BITS + CAN_DST_ID_BITS + CAN_CHANNEL_ID_BITS + CAN_LANE_ID_BITS + CAN_PACKET_ID_BITS + CAN_RETRANS_ID_BITS + CAN_SYNC_FRAME_BITS + CAN_RESERVED_BITS == 29, "Total CAN identifier bits must be 29.");


#ifdef __cplusplus
}
#endif

#endif // PAL_CAN_CONFIG_H