/**
 * @file PAL_Config.h
 * @brief Configuration parameters for the PAL layer.
 */


#ifndef PAL_CONFIG_H
#define PAL_CONFIG_H

#include "../../Config/Include/Config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Receive Mode Configuration
 * @details Defines the receive mode, as specified in protocol-level configuration.
 * Options:
 * - COMM_RX_MODE_INTERRUPT = 1 (Interrupt-driven receive mode)
 * - COMM_RX_MODE_INTERRUPT = 0 (Polling-based receive mode)
 */
#define PAL_RX_MODE_INTERRUPT 1

/**
 * @brief Receive Mailbox Depth
 * @details Defines the depth of the receive mailboxes, as specified in protocol-level configuration.
 *          This determines how many messages can be stored in each mailbox.
 * @warning Must be a power of two for ring buffer implementation. Allowed values: 2, 4, 8, 16, 32, 64, 128, 256.
 */
#define PAL_RX_MAILBOX_DEPTH 64

/**
 * @brief Number of Lanes
 * @details Defines the number of lanes in each channel.
 */
#define PAL_NUM_LANES 2

/**
 * @brief Number of Channels
 * @details Defines the number of communication channels, as specified in protocol-level configuration.
 */
//#define PAL_NUM_CHANNELS NUM_CHANNELS

/**
 * @brief Number of Mailboxes
 * @details Defines the number of mailboxes, calculated as the product of the number of channels and lanes.
 */
//#define PAL_NUM_MAILBOXES (PAL_NUM_CHANNELS * PAL_NUM_LANES)

/**
 * @brief Virtual Serial Baudrate
 * @details Defines the baudrate for the virtual serial console, as specified in protocol-level configuration
 */
#define PAL_VIRTUAL_SERIAL_BAUDRATE 9600U


/**
 * @brief Compile-time assertions to validate configuration parameters.
 **/
CONFIG_ASSERT(PAL_RX_MODE_INTERRUPT == 0 || PAL_RX_MODE_INTERRUPT == 1, "PAL_RX_MODE_INTERRUPT must be either 0 (polling) or 1 (interrupt-driven).");
CONFIG_ASSERT((PAL_RX_MAILBOX_DEPTH != 0) && ((PAL_RX_MAILBOX_DEPTH & (PAL_RX_MAILBOX_DEPTH - 1)) == 0), "PAL_RX_MAILBOX_DEPTH must be a power of two.");
CONFIG_ASSERT((PAL_RX_MAILBOX_DEPTH <= 256), "PAL_RX_MAILBOX_DEPTH must be less than or equal to 256.");
//CONFIG_ASSERT((PAL_NUM_MAILBOXES != 0), "PAL_NUM_MAILBOXES must be greater than zero.");
CONFIG_ASSERT(PAL_VIRTUAL_SERIAL_BAUDRATE == 9600 || PAL_VIRTUAL_SERIAL_BAUDRATE == 115200, "PAL_VIRTUAL_SERIAL_BAUDRATE must be either 9600 or 115200.");


#ifdef __cplusplus
}
#endif

#endif // PAL_CONFIG_H