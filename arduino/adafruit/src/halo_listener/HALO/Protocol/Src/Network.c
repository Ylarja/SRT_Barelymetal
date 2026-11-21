#include "../Private/Network.h"
#include "../../PAL/Private/PAL_CAN_Config.h"
#include "../../../Core/Services/Buffers/Include/Ring_Buffer.h"
#include <stdlib.h>
#include <string.h>

/********************************************************************
 * @par Network Layer Defines
 *******************************************************************/

#define NETWORK_HEADER_SIZE 4 // SRC(1) + DST(1) + LOCAL_CHANNEL_ID(1) + LANE ID(1)
#define NETWORK_REQUEST_EVENT_CAPACITY PAL_RX_MAILBOX_DEPTH

// Offsets for RX network header buffer
#define NETWORK_RX_SRC_ID_OFFSET 0
#define NETWORK_RX_DST_ID_OFFSET 1
#define NETWORK_RX_LOC_CH_ID_OFFSET 2
#define NETWORK_RX_LANE_ID_OFFSET 3

// Offsets for TX network header buffer
#define NETWORK_TX_SRC_ID_OFFSET 3
#define NETWORK_TX_DST_ID_OFFSET 2
#define NETWORK_TX_LOC_CH_ID_OFFSET 1
#define NETWORK_TX_LANE_ID_OFFSET 0

// Other params
#define NUM_LANES 2

// Tag for debug logs
#define NETWORK_TAG "[NET] "
#define TX_TAG "[TX] "
#define RX_TAG "[RX] "

/*****************************************************************
 * @par Sync Domain Types and Data
 *****************************************************************/

// Maximum age of a sync offset before it is considered stale.
// If no FOLLOW_UP has been received within this window, the offset is
// rejected and RC_OK_NO_DATA is returned. Set to at least 2x the
// expected sync period to tolerate one missed cycle.
#define SYNC_VALIDITY_US (3ULL * HALO_SYNC_PERIOD_US)  // 3x sync period — tolerates one missed cycle

// Upper bound on distinct controllers a single node can respond to (= sync
// domains). Sized to the node-ID space so the init-time discovery scan can
// never overflow this scratch array; the live table is allocated to the
// actual count found.
#define MAX_SYNC_DOMAINS 128

typedef struct {
    uint8_t  ctrl_id;        // Controller node ID for this time domain
    uint8_t  pending_seq;    // Sequence number from the last received SYNC frame
    uint32_t t_rx_local;     // Local arrival time of the SYNC frame (us, 32-bit), captured in the true RX ISR by the HAL and passed up via the PAL
    bool     sync_received;  // true = SYNC received, waiting for matching FOLLOW_UP
    int32_t  offset;         // Controller time - local time (us, 32-bit, wrap-safe)
    bool     offset_valid;   // true once the first FOLLOW_UP has been processed
    uint32_t last_sync_us;   // Local time (us, 32-bit) when offset was last updated
} sync_domain_t;

static sync_domain_t *sync_domains     = NULL;
static uint8_t        num_sync_domains = 0;
static uint8_t        sync_seq         = 0;   // Rolling TX sequence counter (controller side)

/*****************************************************************
 * @par Network Layer Status LED Implementation
 *****************************************************************/

#ifndef HALO_DISABLE_LEDS

static const uint64_t led_on_duration  = 100;
static uint64_t led_last_on = 0;

// Red "sync lost" indicator. While any responder sync domain is stale or has
// never locked, the green activity LED is suppressed and the red LED blinks at
// ~2 Hz instead. sync_missing is owned by Sync_Poll() and read by the green-LED
// helpers, so the two indicators never drive the LED at the same time. This also
// keeps them mutually exclusive on single-pixel HALs (e.g. the Feather NeoPixel).
static const uint64_t red_blink_half_period = 250000; // us -> 250 ms on / 250 ms off
static uint64_t red_last_toggle = 0;
static bool     red_on          = false;
static bool     sync_missing    = false;

static inline void LED_Event(void)
{
    if (sync_missing) return;   // activity LED suppressed while sync is lost
    HAL_LED_RGB_Green_On();
    led_last_on = Get_Micros();
}

static inline void LED_Poll(void)
{
    if (sync_missing) return;   // green LED owned by the sync-lost blink

    uint64_t now = Get_Micros();

   if (now - led_last_on >= led_on_duration)
        HAL_LED_RGB_Green_Off();        
}

// Walks the responder sync-domain table and drives the red "sync lost" LED.
// If any domain is unlocked or its offset has gone stale (older than
// SYNC_VALIDITY_US), the green activity LED is held off (see LED_Event/LED_Poll)
// and the red LED blinks at ~2 Hz. Controllers and nodes with no responder
// channels have an empty table, so this is a no-op for them and red stays off.
static void Sync_Poll(void)
{
    bool missing = false;
    for (uint8_t i = 0; i < num_sync_domains; i++) {
        CRITICAL_SECTION_ENTER();
        bool     valid        = sync_domains[i].offset_valid;
        uint32_t last_sync_us = sync_domains[i].last_sync_us;
        CRITICAL_SECTION_EXIT();

        if (!valid || (uint32_t)((uint32_t)Get_Micros() - last_sync_us) > SYNC_VALIDITY_US) {
            missing = true;
            break;
        }
    }

    if (missing) {
        if (!sync_missing) {
            // Entering sync-lost: take the LED from green, start the red blink.
            sync_missing    = true;
            HAL_LED_RGB_Green_Off();
            red_on          = true;
            red_last_toggle = Get_Micros();
            HAL_LED_RGB_Red_On();
        } else {
            uint64_t now = Get_Micros();
            if (now - red_last_toggle >= red_blink_half_period) {
                red_last_toggle = now;
                red_on = !red_on;
                if (red_on) HAL_LED_RGB_Red_On();
                else        HAL_LED_RGB_Red_Off();
            }
        }
    } else if (sync_missing) {
        // Recovered: stop blinking and release the LED back to green activity.
        sync_missing = false;
        red_on       = false;
        HAL_LED_RGB_Red_Off();
    }
}

#endif // HALO_DISABLE_LEDS

/******************************************************************
 * @par Network Layer Implementation
 ******************************************************************/

static const halo_node_t *node = NULL; // Pointer to local node description

typedef struct {
    uint32_t timestamp;
    uint8_t tag;
} listener_event_t;

typedef struct {
    ring_buffer_t timestamps;
    listener_event_t *event_storage;
    bool initialized;
} listener_event_queue_t;

// Indexed by the node's channel_key. Only listener-channel entries are initialized.
static listener_event_queue_t *request_event_queues = NULL;


static inline bool Network_Is_Listener_Channel(const halo_channel_t *channel) {
    return channel->controller->id != node->id && channel->responder->id != node->id;
}


static return_code_t Network_Init_Event_Queue(listener_event_queue_t *queue,
                                              const halo_channel_t *channel,
                                              const char *event_name) {
    queue->event_storage = (listener_event_t *)malloc(NETWORK_REQUEST_EVENT_CAPACITY * sizeof(listener_event_t));
    if (queue->event_storage == NULL) {
        DEBUG_LOG_ERROR(NETWORK_TAG "Failed to allocate %s timestamp queue for channel %d.\n",
                        event_name, channel->global_id);
        return RC_FATAL;
    }

    return_code_t rc = Ring_Buffer_Init(&queue->timestamps, queue->event_storage,
                                        NETWORK_REQUEST_EVENT_CAPACITY, sizeof(listener_event_t));
    if (rc != RC_OK) {
        DEBUG_LOG_ERROR(NETWORK_TAG "Failed to initialize %s timestamp queue for channel %d. RC: %s\n",
                        event_name, channel->global_id, RC_To_String(rc));
        return RC_FATAL;
    }

    queue->initialized = true;
    return RC_OK;
}

static return_code_t Network_Init_Listener_Event_Queues(void) {
    bool has_listener_channel = false;
    for (uint8_t i = 0; i < node->num_channels; i++) {
        const halo_channel_t *channel = node->channels[i];
        if (Network_Is_Listener_Channel(channel)) {
            has_listener_channel = true;
            if (channel->lane0_request_message == NULL ||
                channel->lane0_request_message->message_size > CAN_DATA_MAX_BYTES) {
                DEBUG_LOG_ERROR(NETWORK_TAG "Listener channel %d has an invalid or multi-frame lane 0 request descriptor.\n",
                                channel->global_id);
                return RC_INTEGRITY_FAIL;
            }
        }
    }

    if (!has_listener_channel) {
        return RC_OK;
    }

    request_event_queues = (listener_event_queue_t *)calloc(node->num_channels, sizeof(listener_event_queue_t));
    if (request_event_queues == NULL) {
        DEBUG_LOG_ERROR(NETWORK_TAG "Failed to allocate listener request event queues.\n");
        return RC_FATAL;
    }

    for (uint8_t i = 0; i < node->num_channels; i++) {
        if (!Network_Is_Listener_Channel(node->channels[i])) {
            continue;
        }
        return_code_t rc = Network_Init_Event_Queue(&request_event_queues[i], node->channels[i], "request");
        if (rc != RC_OK) {
            return rc;
        }
    }

    return RC_OK;
}

// Called by the PAL from its RX handler before the CAN frame is queued.
// Records only Lane 0 requests (controller -> responder) on listener channels: they are
// single-frame and the listener's Transport discards them, so this queue is the only
// place where they are observed. Responses and Lane 1 messages carry their timestamp
// through the mailbox to the Transport, together with the frame.
static void Network_Data_Frame_Callback(uint8_t src_id, uint8_t dst_id,
                                        uint8_t local_ch, uint8_t lane, uint8_t packet_id,
                                        uint8_t tag,
                                        uint32_t timestamp) {
    (void)packet_id;

    if (node == NULL || request_event_queues == NULL || lane != 0) {
        return;
    }

    for (uint8_t i = 0; i < node->num_channels; i++) {
        const halo_channel_t *channel = node->channels[i];
        if (!Network_Is_Listener_Channel(channel) || channel->local_id != local_ch) {
            continue;
        }
        // Several channels can share the same local_id: the src/dst pair identifies the channel.
        if (src_id != (uint8_t)channel->controller->id ||
            dst_id != (uint8_t)channel->responder->id) {
            continue;
        }

        listener_event_queue_t *queue = &request_event_queues[i];
        if (queue->initialized) {
            listener_event_t event = {
                .timestamp = timestamp,
                .tag = tag
            };
            return_code_t rc = Ring_Buffer_Push(&queue->timestamps, &event);
            if (rc != RC_OK) {
                DEBUG_LOG_ERROR(NETWORK_TAG "Request event queue full for listener channel %d; event dropped.\n",
                                channel->global_id);
            }
        }
        return;
    }
}

// ISR-context callback: called by PAL when a sync frame is received.
// Updates the appropriate sync domain with timestamp data.
static void Network_Sync_ISR_Callback(uint8_t src_id, uint8_t seq, uint8_t sync_type, uint32_t timestamp) {
    for (uint8_t i = 0; i < num_sync_domains; i++) {
        if (sync_domains[i].ctrl_id == src_id) {
            switch (sync_type) {
                case CAN_SYNC_TYPE_SYNC:
                    // timestamp == local arrival time captured in the true RX ISR
                    // (provided by the PAL/HAL), not a Get_Micros() call here which
                    // would be thread-dispatch time on a deferred-RX platform.
                    sync_domains[i].t_rx_local    = timestamp;
                    sync_domains[i].pending_seq   = seq;
                    sync_domains[i].sync_received = true;
                    break;
                case CAN_SYNC_TYPE_FOLLOW_UP:
                    if (sync_domains[i].sync_received && sync_domains[i].pending_seq == seq) {
                        // Wrap-safe: both timestamps are uint32_t in the same clock domain
                        sync_domains[i].offset        = (int32_t)(timestamp - sync_domains[i].t_rx_local);
                        sync_domains[i].offset_valid  = true;
                        sync_domains[i].sync_received = false;
                        sync_domains[i].last_sync_us  = (uint32_t)Get_Micros();
                        DEBUG_LOG_TRACE(NETWORK_TAG "Sync offset updated for ctrl %d: %ld us\n", src_id, (long)sync_domains[i].offset);
                    }
                    break;
                default:
                    // CAN_SYNC_TYPE_AUTH and CAN_SYNC_TYPE_RESERVED: not yet implemented
                    break;
            }
            return;
        }
    }
}

return_code_t Network_Init(const halo_node_t *node_desc) {

    DEBUG_LOG_TRACE(NETWORK_TAG "Initializing Network Layer.\n");

#ifndef HALO_DISABLE_LEDS
    (void)HAL_LED_RGB_Init();
#endif

    if (node_desc == NULL) {
        DEBUG_LOG_ERROR(NETWORK_TAG "NULL node descriptor passed to Network_Init.\n");
        return RC_FATAL; 
    }

    node = node_desc;

    return_code_t rc = Network_Init_Listener_Event_Queues();
    if (rc != RC_OK) {
        return rc;
    }

    // Register before PAL_Platform_Init: it starts the RX handler, which could
    // receive data frames as soon as the CAN interface is enabled.
    rc = PAL_Register_Data_Frame_Callback(Network_Data_Frame_Callback);
    if (rc != RC_OK) {
        DEBUG_LOG_ERROR(NETWORK_TAG "Failed to register data-frame callback. RC: %s\n", RC_To_String(rc));
        return RC_FATAL;
    }

    rc = PAL_Platform_Init(node);

    if (rc != RC_OK) {
        DEBUG_LOG_ERROR(NETWORK_TAG "PAL initialization failed. RC: %s\n", RC_To_String(rc));
        return RC_FATAL;
    }

    // Build sync domain table: one entry per unique controller this node responds to.
    // Controllers are identified by walking the channel list for responder channels.
    {
        uint8_t temp_ctrl_ids[MAX_SYNC_DOMAINS];
        uint8_t domain_count = 0;

        for (uint8_t i = 0; i < node->num_channels; i++) {
                if (node->channels[i]->controller->id != node->id) {   // responder O listener (era: responder->id == node->id) {
                uint8_t ctrl_id = (uint8_t)node->channels[i]->controller->id;
                bool found = false;
                for (uint8_t j = 0; j < domain_count; j++) {
                    if (temp_ctrl_ids[j] == ctrl_id) { found = true; break; }
                }
                if (!found && domain_count < MAX_SYNC_DOMAINS) {
                    temp_ctrl_ids[domain_count++] = ctrl_id;
                }
            }
        }

        if (domain_count > 0) {
            sync_domains = (sync_domain_t *)malloc(domain_count * sizeof(sync_domain_t));
            if (sync_domains == NULL) {
                DEBUG_LOG_ERROR(NETWORK_TAG "Failed to allocate sync domain table.\n");
                return RC_FATAL;
            }
            memset(sync_domains, 0, domain_count * sizeof(sync_domain_t));
            for (uint8_t i = 0; i < domain_count; i++) {
                sync_domains[i].ctrl_id = temp_ctrl_ids[i];
            }
            num_sync_domains = domain_count;
            PAL_Register_Sync_Callback(Network_Sync_ISR_Callback);
            DEBUG_LOG_INFO(NETWORK_TAG "Registered sync callback for %d controller domain(s).\n", domain_count);
        }
    }

    DEBUG_LOG_INFO(NETWORK_TAG "Successfully initialized Network Layer for Node ID: %d\n", node->id);

    return RC_OK;
}

static return_code_t Network_Get_Listener_Event(listener_event_queue_t *queues,
                                                uint8_t channel_key,
                                                uint32_t *timestamp_out,
                                                uint8_t *tag_out) {
    if (node == NULL) {
        return RC_FATAL;
    }
    if (timestamp_out == NULL || tag_out == NULL) {
        return RC_INVALID_ARG;
    }
    if (channel_key >= node->num_channels) {
        return RC_INVALID_ARG;
    }
    if (!Network_Is_Listener_Channel(node->channels[channel_key])) {
        return RC_PERM_DENIED;
    }
    if (queues == NULL || !queues[channel_key].initialized) {
        return RC_INTEGRITY_FAIL;
    }

    listener_event_t event;
    return_code_t rc = Ring_Buffer_Pop(&queues[channel_key].timestamps, &event);
    if (rc != RC_OK) {
        return rc;
    }

    *timestamp_out = event.timestamp;
    *tag_out = event.tag;
    return RC_OK;
}

return_code_t Network_Get_Request_Event(uint8_t channel_key, uint32_t *timestamp_out,
                                       uint8_t *tag_out) {
    return Network_Get_Listener_Event(request_event_queues, channel_key, timestamp_out, tag_out);
}


void Network_Poll(void) {
#ifndef HALO_DISABLE_LEDS
    LED_Poll();
    Sync_Poll();
#endif

    PAL_Service_Sync();
}

return_code_t Network_Send(lifo_buffer_t *pbuf, uint8_t channel_key, uint8_t lane_id) {

    DEBUG_LOG_TRACE(NETWORK_TAG TX_TAG "[CH %d][L %d] Attempting to send packet.\n", node->channels[channel_key]->global_id, lane_id);

    if(channel_key >= node->num_channels || channel_key < 0 || lane_id >= NUM_LANES || lane_id < 0) {

        DEBUG_LOG_ERROR(NETWORK_TAG TX_TAG "[CH %d][L %d] Invalid channel key or lane ID. Channel Key: %d.\n", node->channels[channel_key]->global_id, lane_id, channel_key);

        return RC_INTEGRITY_FAIL;
    }   

    if (pbuf == NULL) {

        DEBUG_LOG_ERROR(NETWORK_TAG TX_TAG "[CH %d][L %d] NULL buffer pointer passed to Network_Send.\n", node->channels[channel_key]->global_id, lane_id);

        return RC_FATAL;
    }

    const halo_channel_t *channel = node->channels[channel_key];
    uint8_t local_channel_id = channel->local_id;
    uint8_t remote_node_id = 0;

    if (channel->controller->id == node->id) {
        remote_node_id = channel->responder->id;
    } else if (channel->responder->id == node->id) {
        remote_node_id = channel->controller->id;
    } else {

        DEBUG_LOG_ERROR(NETWORK_TAG TX_TAG "[CH %d][L %d] Channel key %d does not correspond to a valid controller or responder endpoint for local node.\n", channel->global_id, lane_id, channel_key);

        return RC_INTEGRITY_FAIL;
    }

    uint8_t network_header[NETWORK_HEADER_SIZE];
    network_header[NETWORK_TX_SRC_ID_OFFSET] = node->id;
    network_header[NETWORK_TX_DST_ID_OFFSET] = remote_node_id;
    network_header[NETWORK_TX_LOC_CH_ID_OFFSET] = local_channel_id;
    network_header[NETWORK_TX_LANE_ID_OFFSET] = lane_id;

    return_code_t rc;

    rc = LIFO_Buffer_Push_N(pbuf, network_header, NETWORK_HEADER_SIZE);
    if (rc != RC_OK) {

        DEBUG_LOG_ERROR(NETWORK_TAG TX_TAG "[CH %d][L %d] Failed to push network header into buffer. RC: %s\n", node->channels[channel_key]->global_id, lane_id, RC_To_String(rc));

        return RC_FATAL;
    }

    rc = PAL_Send(pbuf);
    if (rc != RC_OK) {

        DEBUG_LOG_ERROR(NETWORK_TAG TX_TAG "[CH %d][L %d] PAL_Send failed. RC: %s\n", node->channels[channel_key]->global_id, lane_id, RC_To_String(rc));

        return RC_ERROR;
    }

#ifndef HALO_DISABLE_LEDS
    LED_Event();
#endif

    DEBUG_LOG_TRACE(NETWORK_TAG TX_TAG "[CH %d][L %d] Successfully sent packet to Remote Node ID: %d\n", node->channels[channel_key]->global_id, lane_id, remote_node_id);

    return RC_OK;

}

return_code_t Network_Receive(lifo_buffer_t *pbuf, uint8_t channel_key, uint8_t lane_id,uint32_t *rx_timestamp_us) {

    DEBUG_LOG_TRACE(NETWORK_TAG RX_TAG "[CH %d][L %d] Attempting to receive packet.\n", node->channels[channel_key]->global_id, lane_id);

    if(channel_key >= node->num_channels || channel_key < 0 || lane_id >= NUM_LANES || lane_id < 0) {

        DEBUG_LOG_ERROR(NETWORK_TAG RX_TAG "[CH %d][L %d] Invalid channel key or lane ID. Channel Key: %d, Lane ID: %d\n", node->channels[channel_key]->global_id, lane_id, channel_key, lane_id);

        return RC_INTEGRITY_FAIL;
    }

    if (pbuf == NULL) {

        DEBUG_LOG_ERROR(NETWORK_TAG RX_TAG "[CH %d][L %d] NULL buffer pointer passed to Network_Receive.\n", node->channels[channel_key]->global_id, lane_id);

        return RC_FATAL;
    }

#ifndef HALO_DISABLE_LEDS
    LED_Poll();
#endif

    return_code_t rc;
    const halo_channel_t *channel = node->channels[channel_key];
    
    rc = PAL_Receive(pbuf, channel->global_id, lane_id, rx_timestamp_us);
    if (rc == RC_OK_NO_DATA) {
        DEBUG_LOG_TRACE(NETWORK_TAG RX_TAG "[CH %d][L %d] No data available to receive.\n", channel->global_id, lane_id);
        return rc;
    } 
    if (rc != RC_OK) {
        DEBUG_LOG_ERROR(NETWORK_TAG RX_TAG "[CH %d][L %d] PAL_Receive failed. RC: %s\n", channel->global_id, lane_id, RC_To_String(rc));
        return RC_FATAL;
    }

    uint8_t remote_node_id = 0;
        const bool is_listener = (channel->controller->id != node->id) && (channel->responder->id != node->id);

    uint8_t expected_src;
    uint8_t expected_dst;
    if (!is_listener) {
        expected_src = (channel->controller->id == node->id) ? (uint8_t)channel->responder->id : (uint8_t)channel->controller->id;
        expected_dst = node->id;
    } else if (lane_id == 0) {          // listener, lane 0: risposte
        expected_src = (uint8_t)channel->responder->id;
        expected_dst = (uint8_t)channel->controller->id;
    } else {                            // listener, lane 1: controller -> responder
        expected_src = (uint8_t)channel->controller->id;
        expected_dst = (uint8_t)channel->responder->id;
    }

    uint8_t network_header[NETWORK_HEADER_SIZE];
    rc = LIFO_Buffer_Pop_N(pbuf, network_header, NETWORK_HEADER_SIZE);
    if (rc != RC_OK) {
        DEBUG_LOG_ERROR(NETWORK_TAG RX_TAG "[CH %d][L %d] Failed to pop network header from buffer. RC: %s\n", channel->global_id, lane_id, RC_To_String(rc));
        return RC_FATAL;
    }

    if (network_header[NETWORK_RX_LOC_CH_ID_OFFSET] != channel->local_id) {
        DEBUG_LOG_ERROR(NETWORK_TAG RX_TAG "[CH %d][L %d] Local channel ID mismatch. Expected: %d, Received: %d\n", channel->global_id, lane_id, channel->local_id, network_header[NETWORK_RX_LOC_CH_ID_OFFSET]);
        return RC_INTEGRITY_FAIL;
    }
    if (network_header[NETWORK_RX_LANE_ID_OFFSET] != lane_id) {
        DEBUG_LOG_ERROR(NETWORK_TAG RX_TAG "[CH %d][L %d] Lane ID mismatch. Expected: %d, Received: %d\n", channel->global_id, lane_id, lane_id, network_header[NETWORK_RX_LANE_ID_OFFSET]);
        return RC_INTEGRITY_FAIL;
    }

    // Listener: frame del flusso opposto sulla stessa lane (richieste su lane 0): scartato, non è un errore
    if (is_listener && network_header[NETWORK_RX_SRC_ID_OFFSET] == expected_dst &&
                       network_header[NETWORK_RX_DST_ID_OFFSET] == expected_src) {
        return RC_OK_NO_DATA;
    }

    if (network_header[NETWORK_RX_DST_ID_OFFSET] != expected_dst) {
        DEBUG_LOG_ERROR(NETWORK_TAG RX_TAG "[CH %d][L %d] Destination ID mismatch. Expected: %d, Received: %d\n", channel->global_id, lane_id, expected_dst, network_header[NETWORK_RX_DST_ID_OFFSET]);
        return RC_INTEGRITY_FAIL;
    }
    if (network_header[NETWORK_RX_SRC_ID_OFFSET] != expected_src) {
        DEBUG_LOG_ERROR(NETWORK_TAG RX_TAG "[CH %d][L %d] Source ID mismatch. Expected: %d, Received: %d\n", channel->global_id, lane_id, expected_src, network_header[NETWORK_RX_SRC_ID_OFFSET]);
        return RC_INTEGRITY_FAIL;
    }

#ifndef HALO_DISABLE_LEDS
    LED_Event();
#endif

    DEBUG_LOG_TRACE(NETWORK_TAG RX_TAG "[CH %d][L %d] Successfully received packet from Remote Node ID: %d\n", channel->global_id, lane_id, remote_node_id);
    return RC_OK;

}

return_code_t Network_Is_Data_Available(uint8_t channel_key, uint8_t lane_id) { 

    const halo_channel_t *channel = node->channels[channel_key];

    DEBUG_LOG_TRACE(NETWORK_TAG RX_TAG "[CH %d][L %d] Checking for data availability.\n", channel->global_id, lane_id);

    return PAL_Is_Data_Available(channel->global_id, lane_id); 

}

return_code_t Network_Send_Sync(void) {

    if (node == NULL) {
        return RC_FATAL;
    }

    // Send SYNC frame. PAL stores the follow-up context internally.
    // The FOLLOW_UP frame is dispatched by Network_Poll() once the TX ISR fires.
    CRITICAL_SECTION_ENTER();
    uint8_t seq = sync_seq;
    sync_seq = (uint8_t)((sync_seq + 1) & CAN_PACKET_ID_MASK);
    CRITICAL_SECTION_EXIT();

    return_code_t rc = PAL_Send_Sync(node->id, CAN_SYNC_BROADCAST_DST_ID, seq);
    if (rc != RC_OK) {
        DEBUG_LOG_ERROR(NETWORK_TAG "Network_Send_Sync: SYNC frame failed. RC: %s\n", RC_To_String(rc));
        return RC_ERROR;
    }

    DEBUG_LOG_TRACE(NETWORK_TAG "SYNC frame queued (seq=%d). FOLLOW_UP pending TX ISR.\n", seq);
    return RC_OK;
}

return_code_t Network_Get_Controller_Time(uint8_t ctrl_id, uint32_t *time_out) {

    if (time_out == NULL) {
        return RC_INVALID_ARG;
    }

    for (uint8_t i = 0; i < num_sync_domains; i++) {
        if (sync_domains[i].ctrl_id == ctrl_id) {
            // Read offset and validity atomically w.r.t. the RX ISR
            CRITICAL_SECTION_ENTER();
            bool     valid        = sync_domains[i].offset_valid;
            int32_t  offset       = sync_domains[i].offset;
            uint32_t last_sync_us = sync_domains[i].last_sync_us;
            CRITICAL_SECTION_EXIT();

            if (!valid) {
                return RC_OK_NO_DATA;
            }
            if ((uint32_t)((uint32_t)Get_Micros() - last_sync_us) > SYNC_VALIDITY_US) {
                return RC_OK_NO_DATA; // Offset stale — controller may be offline
            }
            *time_out = (uint32_t)Get_Micros() + (uint32_t)offset;
            return RC_OK;
        }
    }

    return RC_OK_NO_DATA; // No valid sync received yet from this controller
}