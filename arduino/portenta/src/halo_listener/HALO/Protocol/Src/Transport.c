#include "../Private/Transport.h"
#include "../../Config/Include/Config.h"
#include "../../PAL/Private/PAL_CAN_Config.h"
#include <stdlib.h>

/***********************************************************************************************************************************
 * @par Debug Log Tags
 ***********************************************************************************************************************************/
#define TRANSPORT_TAG "[TRN] "
#define TX_FSM_TAG "[TX] "
#define RX_FSM_TAG "[RX] "

/***********************************************************************************************************************************
 * @par Transport Defines
 ***********************************************************************************************************************************/
#define SEGMENT_SIZE 8				  // Segment size in bytes
#define TX_MAX_TOTAL_RETRY_CNT 80	  // Max retries per message across all segments
#define TX_MAX_PER_PACKET_RETRY_CNT 20 // Max retries per single segment
#define PROTOCOL_BUFFER_MAX_SIZE 32	  // Max size of protocol buffer
#define NUM_LANES 2					  // Number of lanes per channel
#define START_TX true				  // Flag to indicate start of transmission when polling TX FSM
#define POLL_TX false				  // Flag to indicate polling TX FSM for state progression without starting a new transmission
#define LANE0 0						  // Lane 0 ID
#define LANE1 1						  // Lane 1 ID

/********************************************************************************************************************************
 * @par Shared types definitions
 ********************************************************************************************************************************/

// Zero-copy protocol buffer type (passed up/down through the stack)
typedef struct {
	uint8_t storage[PROTOCOL_BUFFER_MAX_SIZE];
	lifo_buffer_t buffer;
} protocol_buffer_t;

// Operation type enumeration for message descriptor lookup (TX vs RX)
typedef enum { OP_TX, OP_RX } operation_t;

// Message Descriptor Lookup Table Structure
// It contains pointers to message descriptors for each channel, lane, and operation (TX/RX)
// Indexed as entries[operation][channel][lane]
typedef struct {
	bool initialized;
	const msg_desc_t ****entries;
} msg_desc_lookup_table_t;

// Message buffers for all channels and lanes (separate buffers for TX and RX)
// Indexed as tx[channel][lane] and rx[channel][lane]
typedef struct {
	uint8_t ***tx;
	uint8_t ***rx;
} transport_msg_buffers_t;

/********************************************************************************************************************************
 * @par Shared data structures
 ********************************************************************************************************************************/

// Node configuration pointer
static const halo_node_t *node = NULL;

// Shared message buffers for all channels and lanes (separate buffers for TX and RX)
static transport_msg_buffers_t msg_buffers = {.tx = NULL, .rx = NULL};

// Message descriptor lookup table instance
static msg_desc_lookup_table_t msg_desc_lut = {.initialized = false, .entries = NULL};

/*********************************************************************************************************************************
 * @par Shared data structure initialization functions
 *********************************************************************************************************************************/

// Allocate message buffers for all channels and lanes based on the message descriptor inside the message descriptor lookup table.
// Uses the message descriptor's object_size (i.e., message size in memory, not serialized message size) field to determine
// the required buffer size for each channel and lane.
// Separate buffers are allocated for TX and RX.
static inline return_code_t Allocate_Msg_Buffers(void) {

	DEBUG_LOG_TRACE(TRANSPORT_TAG "Allocating message buffers for all channels and lanes\n");

	if (node == NULL) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG "Node configuration pointer is NULL. Cannot allocate message buffers.\n");
		return RC_FATAL;
	}

	uint8_t rows = node->num_channels;
	uint8_t cols = NUM_LANES;

	if (node->num_channels == 0) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG "Node configuration has zero channels. Cannot allocate message buffers.\n");
		return RC_FATAL;
	}

	if (!msg_desc_lut.initialized) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG "Message Descriptor Lookup Table not initialized. Cannot allocate message buffers.\n");
		return RC_FATAL;
	}

	DEBUG_LOG_TRACE(TRANSPORT_TAG "Node has %d channels. Allocating TX buffers for %d channels and %d lanes.\n", node->num_channels, rows, cols);

	msg_buffers.tx = malloc(rows * sizeof(uint8_t **));
	if (!msg_buffers.tx)
		return RC_FATAL;

	uint8_t **tx_ptr_block = malloc(rows * cols * sizeof(uint8_t *));
	if (!tx_ptr_block) {
		free(msg_buffers.tx);
		return RC_FATAL;
	}

	for (uint8_t i = 0; i < rows; i++)
		msg_buffers.tx[i] = tx_ptr_block + i * cols;

	for (uint8_t i = 0; i < rows; i++) {
		for (uint8_t j = 0; j < cols; j++) {
			if (msg_desc_lut.entries[OP_TX][i][j] != NULL) {
				size_t size = msg_desc_lut.entries[OP_TX][i][j]->object_size;
				msg_buffers.tx[i][j] = calloc(1, size); // Use calloc to zero-initialize
				if (!msg_buffers.tx[i][j])
					goto fail_tx;
				// Initialize the descriptor pointer in the TX buffer
				*((const object_desc_t **)msg_buffers.tx[i][j]) = msg_desc_lut.entries[OP_TX][i][j];
			} else {
				msg_buffers.tx[i][j] = NULL;
			}
		}
	}

	DEBUG_LOG_TRACE(TRANSPORT_TAG "Successfully allocated TX message buffers for all channels and lanes. Allocating RX buffers.\n");

	msg_buffers.rx = malloc(rows * sizeof(uint8_t **));
	if (!msg_buffers.rx)
		goto fail_tx;

	uint8_t **rx_ptr_block = malloc(rows * cols * sizeof(uint8_t *));
	if (!rx_ptr_block) {
		free(msg_buffers.rx);
		goto fail_tx;
	}

	for (uint8_t i = 0; i < rows; i++)
		msg_buffers.rx[i] = rx_ptr_block + i * cols;

	for (uint8_t i = 0; i < rows; i++) {
		for (uint8_t j = 0; j < cols; j++) {
			if (msg_desc_lut.entries[OP_RX][i][j] != NULL) {
				size_t size = msg_desc_lut.entries[OP_RX][i][j]->object_size;
				msg_buffers.rx[i][j] = calloc(1, size); // Use calloc to zero-initialize
				if (!msg_buffers.rx[i][j])
					goto fail_rx;
				// Initialize the descriptor pointer in the RX buffer
				*((const object_desc_t **)msg_buffers.rx[i][j]) = msg_desc_lut.entries[OP_RX][i][j];
			} else {
				msg_buffers.rx[i][j] = NULL;
			}
		}
	}

	DEBUG_LOG_TRACE(TRANSPORT_TAG "Successfully allocated RX message buffers for all channels and lanes.\n");

	DEBUG_LOG_INFO(TRANSPORT_TAG "Successfully allocated TX and RX message buffers for all channels and lanes.\n");

	return RC_OK;

fail_rx:
	for (uint8_t i = 0; i < rows; i++)
		for (uint8_t j = 0; j < cols; j++)
			free(msg_buffers.rx[i][j]);
	free(rx_ptr_block);
	free(msg_buffers.rx);

fail_tx:
	for (uint8_t i = 0; i < rows; i++)
		for (uint8_t j = 0; j < cols; j++)
			free(msg_buffers.tx[i][j]);
	free(tx_ptr_block);
	free(msg_buffers.tx);

	return RC_FATAL;
}

// Allocate the message descriptor lookup table based on the node configuration.
static inline return_code_t Allocate_Msg_Desc_Lookup_Table(void) {

	DEBUG_LOG_TRACE(TRANSPORT_TAG "Allocating message descriptor lookup table\n");

	if (node == NULL) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG "Node configuration pointer is NULL. Cannot allocate message descriptor lookup table.\n");
		return RC_FATAL;
	}

	const uint8_t NUM_OPS = 2;
	uint8_t rows = node->num_channels;

	if (node->num_channels == 0) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG "Node configuration has zero channels. Cannot allocate message descriptor lookup table.\n");
		return RC_FATAL;
	}

	msg_desc_lut.entries = malloc(NUM_OPS * sizeof(msg_desc_t ***));

	if (!msg_desc_lut.entries)
		goto fail;

	for (uint8_t op = 0; op < NUM_OPS; op++) {
		msg_desc_lut.entries[op] = malloc(rows * sizeof(msg_desc_t **));

		if (!msg_desc_lut.entries[op])
			goto fail;

		const msg_desc_t **lane_block = malloc(rows * NUM_LANES * sizeof(msg_desc_t *));

		if (!lane_block)
			goto fail;

		for (uint8_t ch = 0; ch < rows; ch++)
			msg_desc_lut.entries[op][ch] = lane_block + ch * NUM_LANES;
	}

	DEBUG_LOG_INFO(TRANSPORT_TAG "Successfully allocated message descriptor lookup table structure.\n");

	return RC_OK;

fail:
	for (int op = 0; op < NUM_OPS; op++) {
		free(msg_desc_lut.entries[op]);
		msg_desc_lut.entries[op] = NULL;
	}
	free(msg_desc_lut.entries);
	msg_desc_lut.entries = NULL;
	return RC_FATAL;
}

// Initialize the message descriptor lookup table by populating it with the appropriate message descriptors
// for each channel, lane, and operation (TX/RX) based on the node configuration.
static inline return_code_t Init_Msg_Desc_Lookup_Table(void) {

	DEBUG_LOG_TRACE(TRANSPORT_TAG "Initializing message descriptor lookup table\n");

	return_code_t rc = Allocate_Msg_Desc_Lookup_Table();
	if (rc != RC_OK) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG "Failed to allocate message descriptor lookup table. RC: %s\n", RC_To_String(rc));
		return RC_FATAL;
	}

		for (uint8_t ch = 0; ch < node->num_channels; ch++) {

		const halo_channel_t *c = node->channels[ch];

		if (node->id == c->controller->id) {
			msg_desc_lut.entries[OP_TX][ch][LANE0] = c->lane0_request_message;
			msg_desc_lut.entries[OP_RX][ch][LANE0] = c->lane0_message;
			msg_desc_lut.entries[OP_TX][ch][LANE1] = c->lane1_message;
			msg_desc_lut.entries[OP_RX][ch][LANE1] = NULL;

		} else if (node->id == c->responder->id) {
			msg_desc_lut.entries[OP_TX][ch][LANE0] = c->lane0_message;
			msg_desc_lut.entries[OP_RX][ch][LANE0] = c->lane0_request_message;
			msg_desc_lut.entries[OP_TX][ch][LANE1] = NULL;
			msg_desc_lut.entries[OP_RX][ch][LANE1] = c->lane1_message;

		} else {
			// Listener: nessun TX. Lane 0 = risposte (responder->controller), lane 1 = controller->responder
			msg_desc_lut.entries[OP_TX][ch][LANE0] = NULL;
			msg_desc_lut.entries[OP_TX][ch][LANE1] = NULL;
			msg_desc_lut.entries[OP_RX][ch][LANE0] = c->lane0_message;
			msg_desc_lut.entries[OP_RX][ch][LANE1] = c->lane1_message;
		}
	}

	msg_desc_lut.initialized = true;

	DEBUG_LOG_INFO(TRANSPORT_TAG "Successfully initialized message descriptor lookup table with message descriptors for all channels and lanes.\n");

	return RC_OK;
}

/***********************************************************************************************************************************
 * @par Helpers
 ***********************************************************************************************************************************/
static inline bool Are_Channel_And_Lane_Valid(uint8_t channel_key, uint8_t lane_id) {
	if (channel_key >= node->num_channels || lane_id >= NUM_LANES || channel_key < 0 || lane_id < 0) {
		return false;
	}
	return true;
}

/********************************************************************************************************************************
 * @par TX FSM Types
 * ******************************************************************************************************************************/

// TX FSM state enumeration
typedef enum { TX_IDLE, TX_SERIALIZE, TX_SEGMENT, TX_SEND, TX_FAILED } tx_state_t;

// TX FSM Context Structure
// Contains all necessary context for managing the state and data of an ongoing transmission on a specific channel and lane
typedef struct {

	// Global Context
	uint16_t channel_id;	// Global Channel ID
	uint8_t lane_id;		// Lane ID
	uint8_t channel_key;	// Channel Key (index into node->channels)
	tx_state_t curr_state;	// Current State
	return_code_t last_rc;	// Last Return Code
	return_code_t out_rc;	// Output Return Code
	protocol_buffer_t pbuf; // Protocol Buffer

	// Event Flags
	bool msg_buffered_event; // Flag to indicate a new message has been buffered and is ready for transmission
	bool done_event;		 // Flag to indicate completion of the current transmission
	bool abort_event;		 // Flag to indicate if the current transmission has been aborted

	// Serialize State Context
	void *msg;			// Pointer to the message being sent (Application owned buffer)
	const msg_desc_t *msg_desc; // Message Descriptor (Used for serialization)

	// Segment State Context
	uint8_t *seg_buffer;   // Segmentation Buffer (Data is serialized here before being segmented)
	uint8_t curr_seg_idx;  // Current Segment Index (Offset of the segment being processed)
	uint8_t last_seg_size; // Size of the last segment relative to the message descriptor size
	uint8_t seg_cnt;	   // Total Number of Segments relative to the message descriptor size
	uint8_t retr_id;	   // Retransmission ID (Increments on each message, not segment, retransmission)
	uint8_t tag;           // Three-bit message tag, repeated on every segment

	// Send State Context
	uint8_t tot_retry_cnt; // Total Retry Count across all segments
	uint8_t pkt_retry_cnt; // Retry Count for the current segment

} tx_context_t;

/******************************************************************************************************************************
 * @par TX FSM Data Structures (Context) And Initialization Functions
 ******************************************************************************************************************************/

// TX context table for all channels and lanes, allocated dynamically based on the node configuration.
// Indexed as tx_ctx_table[channel][lane]
static tx_context_t **tx_ctx_table = NULL;

// Allocate the TX context table based on the node configuration.
// Allocates a 2D array of tx_context_t structures with dimensions [node->num_channels][NUM_LANES].
static inline return_code_t Allocate_Tx_Ctx_Table(void) {

	DEBUG_LOG_TRACE(TRANSPORT_TAG "Allocating TX context table\n");

	if (node == NULL) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG "Node configuration pointer is NULL. Cannot allocate TX context table.\n");
		return RC_FATAL;
	}

	uint8_t rows = node->num_channels;
	uint8_t cols = NUM_LANES;

	if (node->num_channels == 0) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG "Node configuration has zero channels. Cannot allocate TX context table.\n");
		return RC_FATAL;
	}

	// Allocate array of row pointers
	tx_ctx_table = malloc(rows * sizeof(tx_context_t *));
	if (tx_ctx_table == NULL) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG "Failed to allocate memory for TX context table row pointers.\n");
		goto fail;
	}

	// Allocate contiguous data block
	tx_context_t *ctx = NULL;
	ctx = malloc(rows * cols * sizeof(tx_context_t));
	if (ctx == NULL) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG "Failed to allocate memory for TX context table data block.\n");
		goto fail;
	}

	// Assign row pointers into the contiguous block
	for (uint8_t i = 0; i < rows; i++) {
		tx_ctx_table[i] = ctx + i * cols;
	}

	DEBUG_LOG_TRACE(TRANSPORT_TAG "Successfully allocated TX context table with %d rows and %d columns.\n", rows, cols);

	return RC_OK;

fail:
	free(tx_ctx_table);
	free(ctx);
	tx_ctx_table = NULL;
	ctx = NULL;
	return RC_FATAL;
}

/***********************************************************************************************************************************
 * @par TX FSM Implementation
 ***********************************************************************************************************************************/

// Initialize the TX FSM by allocating and setting up the context for each channel and lane
// based on the node configuration, message descriptor lookup table, and message buffers.
static inline return_code_t Init_Tx_FSM(void) {

	DEBUG_LOG_TRACE(TRANSPORT_TAG "Initializing TX FSM\n");

	if (node == NULL) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG "Node configuration is not set. Cannot initialize TX FSM.\n");
		return RC_FATAL;
	}

	if (!msg_desc_lut.initialized) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG "Message descriptor lookup table is not initialized. Cannot initialize TX FSM.\n");
		return RC_FATAL;
	}

	if (msg_buffers.tx == NULL) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG "Message buffers are not allocated. Cannot initialize TX FSM.\n");
		return RC_FATAL;
	}

	return_code_t rc;

	rc = Allocate_Tx_Ctx_Table();
	if (rc != RC_OK) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG "Failed to allocate TX context table. RC: %s\n", RC_To_String(rc));
		return rc;
	}

	for (uint8_t i = 0; i < node->num_channels; i++) {
		for (uint8_t j = 0; j < NUM_LANES; j++) {
			tx_ctx_table[i][j].channel_id = node->channels[i]->global_id;
			tx_ctx_table[i][j].lane_id = j;
			tx_ctx_table[i][j].channel_key = i;
			tx_ctx_table[i][j].msg_desc = msg_desc_lut.entries[OP_TX][i][j];
			tx_ctx_table[i][j].msg = msg_buffers.tx[i][j];

			if (tx_ctx_table[i][j].msg_desc != NULL) {

				tx_ctx_table[i][j].seg_buffer = NULL;
				tx_ctx_table[i][j].seg_buffer = (uint8_t *)malloc(tx_ctx_table[i][j].msg_desc->message_size);
				if (tx_ctx_table[i][j].seg_buffer == NULL) {
					DEBUG_LOG_ERROR(TRANSPORT_TAG "Failed to allocate memory for TX segmentation buffer for Channel ID: %d, Lane ID: %d\n", tx_ctx_table[i][j].channel_id,
									tx_ctx_table[i][j].lane_id);
					return RC_FATAL;
				}

				tx_ctx_table[i][j].seg_cnt = (tx_ctx_table[i][j].msg_desc->message_size + SEGMENT_SIZE - 1) / SEGMENT_SIZE;
				uint8_t remainder = tx_ctx_table[i][j].msg_desc->message_size % SEGMENT_SIZE;
				tx_ctx_table[i][j].last_seg_size = (remainder == 0) ? SEGMENT_SIZE : remainder;
			}

			tx_ctx_table[i][j].done_event = false;
			tx_ctx_table[i][j].msg_buffered_event = false;
			tx_ctx_table[i][j].abort_event = false;

			tx_ctx_table[i][j].curr_seg_idx = 0;
			tx_ctx_table[i][j].retr_id = 0;
			tx_ctx_table[i][j].tag = 0;
			tx_ctx_table[i][j].tot_retry_cnt = 0;
			tx_ctx_table[i][j].pkt_retry_cnt = 0;
			tx_ctx_table[i][j].curr_state = TX_IDLE;
			tx_ctx_table[i][j].last_rc = RC_OK;
			tx_ctx_table[i][j].out_rc = RC_OK;

			lifo_buffer_t *tx_buf = &tx_ctx_table[i][j].pbuf.buffer;
			uint8_t *storage = tx_ctx_table[i][j].pbuf.storage;
			rc = LIFO_Buffer_Init(tx_buf, storage, PROTOCOL_BUFFER_MAX_SIZE, sizeof(uint8_t));
			if (rc != RC_OK) {

				DEBUG_LOG_ERROR(TRANSPORT_TAG "Failed to initialize LIFO buffer for Channel ID: %d, Lane ID: %d. RC: %s\n", tx_ctx_table[i][j].channel_id,
								tx_ctx_table[i][j].lane_id, RC_To_String(rc));

				return rc;
			}

			DEBUG_LOG_TRACE(TRANSPORT_TAG "Initialized TX FSM for Channel ID: %d, Lane ID: %d, Message Size: %d\n", tx_ctx_table[i][j].channel_id, tx_ctx_table[i][j].lane_id,
							(tx_ctx_table[i][j].msg_desc ? tx_ctx_table[i][j].msg_desc->message_size : 0));
		}
	}

	DEBUG_LOG_INFO(TRANSPORT_TAG "TX FSM initialization complete for all channels and lanes.\n");

	return RC_OK;
}

return_code_t Transport_Tx_Reset(uint8_t channel_key, uint8_t lane_id) {

	if (!Are_Channel_And_Lane_Valid(channel_key, lane_id)) {
		return RC_INVALID_ARG;
	}

	tx_context_t *ctx = &tx_ctx_table[channel_key][lane_id];

	ctx->curr_seg_idx = 0;
	ctx->retr_id = 0;
	ctx->tag = 0;
	ctx->tot_retry_cnt = 0;
	ctx->pkt_retry_cnt = 0;
	ctx->curr_state = TX_IDLE;
	ctx->done_event = false;
	ctx->abort_event = false;
	ctx->last_rc = RC_OK;
	ctx->out_rc = RC_OK;

	(void)LIFO_Buffer_Clear(&ctx->pbuf.buffer);

	DEBUG_LOG_INFO(TRANSPORT_TAG TX_FSM_TAG "[CH %d][L %d] TX FSM has been reset.\n", ctx->channel_id, ctx->lane_id);

	return RC_OK;
}

static inline void Tx_Idle_State(tx_context_t *ctx, bool start) {

	lifo_buffer_t *tx_buf = &ctx->pbuf.buffer;

	if (!start) {

		ctx->curr_state = TX_IDLE;
		ctx->out_rc = RC_OK;

	} else {

		(void)LIFO_Buffer_Clear(tx_buf);
		ctx->curr_seg_idx = 0;
		ctx->retr_id = 0;
		ctx->tot_retry_cnt = 0;
		ctx->pkt_retry_cnt = 0;
		ctx->curr_state = TX_SERIALIZE;
		ctx->done_event = false;
		ctx->abort_event = false;
		ctx->last_rc = RC_OK;
		ctx->out_rc = RC_BUSY;

		DEBUG_LOG_TRACE(TRANSPORT_TAG TX_FSM_TAG "[CH %d][L %d] [IDLE] Waiting for start event\n", ctx->channel_id, ctx->lane_id);
	}
}

static inline void Tx_Serialize_State(tx_context_t *ctx) {

	ctx->last_rc = Serialize_Object(ctx->msg, ctx->seg_buffer, ctx->msg_desc);
	if (ctx->last_rc != RC_OK) {
		ctx->curr_state = TX_FAILED;
	} else {
		ctx->curr_state = TX_SEGMENT;
	}

	DEBUG_LOG_TRACE(TRANSPORT_TAG TX_FSM_TAG "[CH %d][L %d] [SERIALIZE] Last RC: %s\n", ctx->channel_id, ctx->lane_id, RC_To_String(ctx->last_rc));
}

static inline void Tx_Segment_State(tx_context_t *ctx) {

	if (ctx->curr_seg_idx == ctx->seg_cnt) {

		ctx->curr_state = TX_IDLE;
		ctx->curr_seg_idx = 0;
		ctx->last_rc = RC_OK;
		ctx->out_rc = RC_OK;
		ctx->done_event = true;
		ctx->abort_event = false;

		DEBUG_LOG_TRACE(TRANSPORT_TAG TX_FSM_TAG "[CH %d][L %d] [SEGMENT] Completed segmentation. Total Segments: %d\n", ctx->channel_id, ctx->lane_id, ctx->seg_cnt);

	} else {

		lifo_buffer_t *tx_buf = &ctx->pbuf.buffer;
		uint8_t seg_size = (ctx->curr_seg_idx == ctx->seg_cnt - 1) ? ctx->last_seg_size : SEGMENT_SIZE;

		(void)LIFO_Buffer_Clear(tx_buf);																// Clear buffer before pushing new segment
		(void)LIFO_Buffer_Push_N(tx_buf, &ctx->seg_buffer[ctx->curr_seg_idx * SEGMENT_SIZE], seg_size); // Push data payload
		(void)LIFO_Buffer_Push(tx_buf, &seg_size);														// Push data length
		(void)LIFO_Buffer_Push(tx_buf, &ctx->tag);														// Push message tag
		(void)LIFO_Buffer_Push(tx_buf, &ctx->retr_id);													// Push message retransmission ID
		(void)LIFO_Buffer_Push(tx_buf, &ctx->curr_seg_idx);												// Push packet ID (sequence number)

		ctx->curr_seg_idx++;
		ctx->curr_state = TX_SEND;
		ctx->last_rc = RC_OK;

		DEBUG_LOG_TRACE(TRANSPORT_TAG TX_FSM_TAG "[CH %d][L %d] [SEGMENT] Current Segment Index: %d. Segment Size: %d. Last RC: %s\n", ctx->channel_id, ctx->lane_id, ctx->curr_seg_idx,
						seg_size, RC_To_String(ctx->last_rc));
	}
}

static inline void Tx_Send_State(tx_context_t *ctx) {

	lifo_buffer_t *tx_buf = &ctx->pbuf.buffer;

	ctx->last_rc = Network_Send(tx_buf, ctx->channel_key, ctx->lane_id);
	if (ctx->last_rc != RC_OK) {
		(ctx->tot_retry_cnt)++;
		(ctx->pkt_retry_cnt)++;
		if ((ctx->pkt_retry_cnt > TX_MAX_PER_PACKET_RETRY_CNT) || (ctx->tot_retry_cnt > TX_MAX_TOTAL_RETRY_CNT)) {
			ctx->last_rc = RC_RETRY_LIMIT;
		}
	}

	if (ctx->last_rc == RC_OK) {
		ctx->pkt_retry_cnt = 0; // Reset per-packet retry count on success
		ctx->curr_state = TX_SEGMENT;
	} else if (ctx->last_rc == RC_RETRY_LIMIT) {
		ctx->curr_state = TX_FAILED;
		ctx->out_rc = RC_FATAL;
		ctx->abort_event = true; // Set abort_event flag to indicate transmission failure due to retry limit
		DEBUG_LOG_ERROR(TRANSPORT_TAG TX_FSM_TAG "[CH %d][L %d] [SEND] Transmission failed due to retry limit. Total Retries: %d, Packet Retries: %d\n", ctx->channel_id, ctx->lane_id, ctx->tot_retry_cnt, ctx->pkt_retry_cnt);
	} else {
		DEBUG_LOG_INFO(TRANSPORT_TAG TX_FSM_TAG "[CH %d][L %d] [SEND] Send failed (RC: %s). Retrying segment %d. Total Retries: %d, Packet Retries: %d\n", ctx->channel_id, ctx->lane_id, RC_To_String(ctx->last_rc), ctx->curr_seg_idx - 1, ctx->tot_retry_cnt, ctx->pkt_retry_cnt);
		ctx->curr_seg_idx--; // Decrement to re-prepare the same segment since the buffer was consumed
		ctx->curr_state = TX_SEGMENT; // Go back to SEGMENT to rebuild the buffer before retrying
	}

	DEBUG_LOG_TRACE(TRANSPORT_TAG TX_FSM_TAG "[CH %d][L %d] [SEND] Last RC: %s\n", ctx->channel_id, ctx->lane_id, RC_To_String(ctx->last_rc));
}

static inline void Tx_Failed_State(tx_context_t *ctx) {

	ctx->curr_state = TX_IDLE;
	ctx->out_rc = RC_FATAL;
	ctx->abort_event = true;
	ctx->done_event = false;
	DEBUG_LOG_TRACE(TRANSPORT_TAG TX_FSM_TAG "[CH %d][L %d] [FAILED] Last RC: %s\n", ctx->channel_id, ctx->lane_id, RC_To_String(ctx->last_rc));
}

static inline return_code_t Tx_FSM_Poll(uint8_t channel_key, uint8_t lane_id, bool start) {

	if (!Are_Channel_And_Lane_Valid(channel_key, lane_id)) {
		return RC_INVALID_ARG;
	}

	tx_context_t *ctx = &tx_ctx_table[channel_key][lane_id];

	switch (ctx->curr_state) {
	case TX_IDLE:
		Tx_Idle_State(ctx, start);
		break;
	case TX_SERIALIZE:
		Tx_Serialize_State(ctx);
		break;
	case TX_SEGMENT:
		Tx_Segment_State(ctx);
		break;
	case TX_SEND:
		Tx_Send_State(ctx);
		break;
	case TX_FAILED:
		Tx_Failed_State(ctx);
		break;
	default:
		Tx_Failed_State(ctx);
		break;
	}

	DEBUG_LOG_TRACE(TRANSPORT_TAG TX_FSM_TAG "[CH %d][L %d] Polled TX FSM. Current State: %d, Last RC: %s, Done Event: %d, Abort Event: %d\n", ctx->channel_id, ctx->lane_id,
					ctx->curr_state, RC_To_String(ctx->last_rc), ctx->done_event, ctx->abort_event);

	return RC_OK;
}

/**********************************************************************************************************************************
 * @par RX FSM Types
 **********************************************************************************************************************************/

typedef enum { RX_IDLE, RX_WAIT, RX_RECEIVE, RX_REASSEMBLE, RX_DESERIALIZE, RX_FAILED } rx_state_t; // RX FSM state enumeration

// RX Reassembly Buffer
typedef struct {
	uint8_t pkt_cnt;	  // Packet Count	(Number of segments received so far)
	uint8_t *data;		  // Reassembly Data Buffer
} rx_reassembly_buffer_t; // RX Reassembly Buffer Structure

// RX FSM Context Structure
typedef struct {

	// Global Context
	uint16_t channel_id;	// Global Channel ID
	uint8_t lane_id;		// Lane ID
	uint8_t channel_key;	// Channel Key (index into node->channels)
	rx_state_t curr_state;	// Current State
	return_code_t last_rc;	// Last Return Code
	protocol_buffer_t pbuf; // Protocol Buffer
	return_code_t out_rc;	// Output Return Code

	// Events Flags
	bool abort_event;		  // Flag to indicate if the reception has been abort_event
	bool overrun_event;		  // Flag to indicate if a buffer overrun_event has occurred during reception
	bool msg_available_event; // Flag to indicate if a complete message has been deserialized and is available for retrieval
	bool tag_available;       // Flag indicating a completed message tag is available
	uint8_t tag;              // Tag being reassembled or belonging to the last complete message
	uint32_t seg_timestamp_us; // Arrival time of the segment currently being processed (from Network_Receive)
	uint32_t msg_timestamp_us; // Arrival time of the LAST segment of the completed message
	uint8_t  msg_tag;          // Tag of the completed message (stable until the next message completes)

	// Deserialize State Context
	void *msg;					// Pointer to the message to populate with received data (Application owned buffer)
	const msg_desc_t *msg_desc; // Message Descriptor (Used for deserialization)

	// Reassembly State Context
	uint8_t seg_cnt;				   // Total Number of Segments relative to the message descriptor size
	rx_reassembly_buffer_t rea_buffer; // RX Reassembly Buffer

} rx_context_t;

/**********************************************************************************************************************************
 * @par RX FSM Data Structures (Context) And Initialization Functions
 **********************************************************************************************************************************/
static rx_context_t **rx_ctx_table = NULL;

static inline return_code_t Allocate_Rx_Ctx_Table() {

	DEBUG_LOG_TRACE(TRANSPORT_TAG "Allocating RX context table\n");

	if (node == NULL) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG "Node configuration pointer is NULL. Cannot allocate RX context table.\n");
		return RC_FATAL;
	}

	uint8_t rows = node->num_channels;
	uint8_t cols = NUM_LANES;

	if (node->num_channels == 0) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG "Node configuration has zero channels. Cannot allocate RX context table.\n");
		return RC_FATAL;
	}

	// Allocate array of row pointers
	rx_ctx_table = malloc(rows * sizeof(rx_context_t *));
	if (rx_ctx_table == NULL) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG "Failed to allocate memory for RX context table row pointers.\n");
		goto fail;
	}

	// Allocate contiguous data block
	rx_context_t *ctx = NULL;
	ctx = malloc(rows * cols * sizeof(rx_context_t));
	if (ctx == NULL) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG "Failed to allocate memory for RX context table data block.\n");
		goto fail;
	}

	// Assign row pointers into the contiguous block
	for (uint8_t i = 0; i < rows; i++) {
		rx_ctx_table[i] = ctx + i * cols;
	}

	DEBUG_LOG_TRACE(TRANSPORT_TAG "Successfully allocated RX context table with %d rows and %d columns.\n", rows, cols);

	return RC_OK;

fail:
	free(rx_ctx_table);
	free(ctx);
	rx_ctx_table = NULL;
	ctx = NULL;
	return RC_FATAL;
}

/**********************************************************************************************************************************
 * @par RX FSM Implementation
 **********************************************************************************************************************************/

static inline return_code_t Init_Rx_FSM(void) {

	DEBUG_LOG_TRACE(TRANSPORT_TAG "Initializing RX FSM\n");

	if (node == NULL) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG "Node configuration is not set. Cannot initialize RX FSM.\n");
		return RC_FATAL;
	}

	if (!msg_desc_lut.initialized) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG "Message descriptor lookup table is not initialized. Cannot initialize RX FSM.\n");
		return RC_FATAL;
	}

	if (msg_buffers.rx == NULL) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG "Message buffers are not allocated. Cannot initialize RX FSM.\n");
		return RC_FATAL;
	}

	return_code_t rc;

	rc = Allocate_Rx_Ctx_Table();
	if (rc != RC_OK) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG "Failed to allocate RX context table. RC: %s\n", RC_To_String(rc));
		return rc;
	}

	for (uint8_t i = 0; i < node->num_channels; i++) {
		for (uint8_t j = 0; j < NUM_LANES; j++) {
			rx_ctx_table[i][j].channel_id = node->channels[i]->global_id;
			rx_ctx_table[i][j].lane_id = j;
			rx_ctx_table[i][j].channel_key = i;
			rx_ctx_table[i][j].msg = msg_buffers.rx[i][j];
			rx_ctx_table[i][j].msg_desc = msg_desc_lut.entries[OP_RX][i][j];

			if (rx_ctx_table[i][j].msg_desc != NULL) {

				rx_ctx_table[i][j].rea_buffer.data = NULL;
				rx_ctx_table[i][j].rea_buffer.data = (uint8_t *)malloc(rx_ctx_table[i][j].msg_desc->message_size);
				if (rx_ctx_table[i][j].rea_buffer.data == NULL) {
					DEBUG_LOG_ERROR(TRANSPORT_TAG "Failed to allocate memory for RX reassembly buffer for Channel ID: %d, Lane ID: %d\n", rx_ctx_table[i][j].channel_id,
									rx_ctx_table[i][j].lane_id);
					return RC_FATAL;
				}

				rx_ctx_table[i][j].seg_cnt = (rx_ctx_table[i][j].msg_desc->message_size + SEGMENT_SIZE - 1) / SEGMENT_SIZE;
			}

			rx_ctx_table[i][j].curr_state = RX_IDLE;
			rx_ctx_table[i][j].last_rc = RC_OK;
			rx_ctx_table[i][j].out_rc = RC_OK;
			rx_ctx_table[i][j].abort_event = false;
			rx_ctx_table[i][j].overrun_event = false;
			rx_ctx_table[i][j].msg_available_event = false;
			rx_ctx_table[i][j].tag_available = false;
			rx_ctx_table[i][j].tag = 0;
			rx_ctx_table[i][j].seg_timestamp_us = 0;
			rx_ctx_table[i][j].msg_timestamp_us = 0;
			rx_ctx_table[i][j].msg_tag = 0;

			lifo_buffer_t *rx_buf = &rx_ctx_table[i][j].pbuf.buffer;
			uint8_t *storage = rx_ctx_table[i][j].pbuf.storage;
			rc = LIFO_Buffer_Init(rx_buf, storage, PROTOCOL_BUFFER_MAX_SIZE, sizeof(uint8_t));
			if (rc != RC_OK) {
				DEBUG_LOG_ERROR(TRANSPORT_TAG "Failed to initialize LIFO buffer for Channel ID: %d, Lane ID: %d. RC: %s\n", rx_ctx_table[i][j].channel_id,
								rx_ctx_table[i][j].lane_id, RC_To_String(rc));
				return rc;
			}

			DEBUG_LOG_TRACE(TRANSPORT_TAG "Initialized RX FSM for Channel ID: %d, Lane ID: %d, Message Size: %d\n", rx_ctx_table[i][j].channel_id, rx_ctx_table[i][j].lane_id,
							(rx_ctx_table[i][j].msg_desc ? rx_ctx_table[i][j].msg_desc->message_size : 0));
		}
	}

	DEBUG_LOG_INFO(TRANSPORT_TAG "RX FSM initialization complete for all channels and lanes.\n");
	return RC_OK;
}

return_code_t Transport_Rx_Reset(uint8_t channel_key, uint8_t lane_id) {

	if (!Are_Channel_And_Lane_Valid(channel_key, lane_id)) {
		return RC_INVALID_ARG;
	}

	rx_context_t *ctx = &rx_ctx_table[channel_key][lane_id];

	ctx->curr_state = RX_IDLE;
	ctx->last_rc = RC_OK;
	ctx->out_rc = RC_OK;
	ctx->abort_event = false;
	ctx->overrun_event = false;
	ctx->msg_available_event = false;
	ctx->tag_available = false;
	ctx->tag = 0;
	ctx->seg_timestamp_us = 0;
	ctx->msg_timestamp_us = 0;
	ctx->msg_tag = 0;
	ctx->rea_buffer.pkt_cnt = 0;
	(void)LIFO_Buffer_Clear(&ctx->pbuf.buffer);

	DEBUG_LOG_INFO(TRANSPORT_TAG RX_FSM_TAG "[CH %d][L %d] RX FSM has been reset.\n", ctx->channel_id, ctx->lane_id);
	return RC_OK;
}

return_code_t Transport_Tx_Set_Tag(uint8_t channel_key, uint8_t lane_id, uint8_t tag) {

	if (!Are_Channel_And_Lane_Valid(channel_key, lane_id) || tag > CAN_TAG_MASK) {
		return RC_INVALID_ARG;
	}

	tx_context_t *ctx = &tx_ctx_table[channel_key][lane_id];
	if (ctx->out_rc == RC_BUSY) {
		return RC_BUSY;
	}

	ctx->tag = tag;
	return RC_OK;
}

return_code_t Transport_Rx_Get_Tag(uint8_t channel_key, uint8_t lane_id, uint8_t *tag_out) {

	if (!Are_Channel_And_Lane_Valid(channel_key, lane_id) || tag_out == NULL) {
		return RC_INVALID_ARG;
	}

	rx_context_t *ctx = &rx_ctx_table[channel_key][lane_id];
	if (!ctx->tag_available) {
		return RC_OK_NO_DATA;
	}

	*tag_out = ctx->tag;
	ctx->tag_available = false;
	return RC_OK;
}

static inline void Rx_Idle_State(rx_context_t *ctx) {

	(void)LIFO_Buffer_Clear(&ctx->pbuf.buffer);
	ctx->curr_state = RX_WAIT;
	ctx->rea_buffer.pkt_cnt = 0;
	ctx->out_rc = RC_OK;

	DEBUG_LOG_TRACE(TRANSPORT_TAG RX_FSM_TAG "[CH %d][L %d] [IDLE] Waiting for data\n", ctx->channel_id, ctx->lane_id);
}

static inline void Rx_Wait_State(rx_context_t *ctx) {

	DEBUG_LOG_TRACE(TRANSPORT_TAG RX_FSM_TAG "[CH %d][L %d] [WAIT] Last RC: %s\n", ctx->channel_id, ctx->lane_id, RC_To_String(ctx->last_rc));

	if (Network_Is_Data_Available(ctx->channel_key, ctx->lane_id) == RC_OK_TRUE) {
		ctx->curr_state = RX_RECEIVE;
		ctx->last_rc = RC_OK;
		ctx->out_rc = RC_BUSY;
	} else {
		ctx->curr_state = RX_WAIT;
	}
}

static inline void Rx_Receive_State(rx_context_t *ctx) {

	DEBUG_LOG_TRACE(TRANSPORT_TAG RX_FSM_TAG "[CH %d][L %d] [RECEIVE] Last RC: %s\n", ctx->channel_id, ctx->lane_id, RC_To_String(ctx->last_rc));

	lifo_buffer_t *rx_buf = &ctx->pbuf.buffer;

	(void)LIFO_Buffer_Clear(rx_buf);
	ctx->last_rc = Network_Receive(rx_buf, ctx->channel_key, ctx->lane_id, &ctx->seg_timestamp_us);
	if (ctx->last_rc == RC_OK) {
		ctx->curr_state = RX_REASSEMBLE;
	} else if (ctx->last_rc == RC_OK_NO_DATA) {
		ctx->curr_state = RX_WAIT;
	} else {
		ctx->curr_state = RX_FAILED;
		ctx->out_rc = RC_FATAL;
		ctx->abort_event = true; // Set abort_event flag to indicate reception failure
	}
}

static inline void Rx_Reassemble_State(rx_context_t *ctx) {

	DEBUG_LOG_TRACE(TRANSPORT_TAG RX_FSM_TAG "[CH %d][L %d] [REASSEMBLE] Last RC: %s\n", ctx->channel_id, ctx->lane_id, RC_To_String(ctx->last_rc));

	lifo_buffer_t *rx_buf = &ctx->pbuf.buffer;

	uint8_t pkt_id = 0;
	(void)LIFO_Buffer_Pop(rx_buf, &pkt_id);
	uint8_t retr_id = 0;
	(void)LIFO_Buffer_Pop(rx_buf, &retr_id);
	uint8_t tag = 0;
	(void)LIFO_Buffer_Pop(rx_buf, &tag);

	if (pkt_id != ctx->rea_buffer.pkt_cnt) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG RX_FSM_TAG "[CH %d][L %d] [REASSEMBLE] Packet ID mismatch. Expected: %d, Received: %d\n", ctx->channel_id, ctx->lane_id,
						ctx->rea_buffer.pkt_cnt, pkt_id);
		ctx->curr_state = RX_RECEIVE;
		ctx->last_rc = RC_INTEGRITY_FAIL;
		return;
	}

	if (ctx->rea_buffer.pkt_cnt == 0) {
		ctx->tag = tag;
	} else if (tag != ctx->tag) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG RX_FSM_TAG "[CH %d][L %d] [REASSEMBLE] Message tag mismatch. Expected: %d, Received: %d\n",
						ctx->channel_id, ctx->lane_id, ctx->tag, tag);
		ctx->curr_state = RX_FAILED;
		ctx->last_rc = RC_INTEGRITY_FAIL;
		return;
	}

	uint8_t dlc = 0;
	(void)LIFO_Buffer_Pop(rx_buf, &dlc);

	uint8_t current_rea_idx = ctx->rea_buffer.pkt_cnt * SEGMENT_SIZE;

	// Validate the wire-supplied segment length before using it as a copy count.
	// A segment holds at most SEGMENT_SIZE bytes, and the write must stay within
	// the reassembly buffer (sized msg_desc->message_size). An out-of-range dlc
	// (lost/duplicated segment or corrupt frame) would otherwise overflow the
	// heap buffer — abort reception instead of writing past it.
	if (dlc > SEGMENT_SIZE || (current_rea_idx + dlc) > ctx->msg_desc->message_size) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG RX_FSM_TAG "[CH %d][L %d] [REASSEMBLE] Invalid segment length: dlc=%d, offset=%d, msg_size=%d. Aborting reception.\n",
						ctx->channel_id, ctx->lane_id, dlc, current_rea_idx, ctx->msg_desc->message_size);
		ctx->curr_state = RX_FAILED;
		ctx->last_rc = RC_INTEGRITY_FAIL;
		return;
	}

	(void)LIFO_Buffer_Pop_N(rx_buf, ctx->rea_buffer.data + current_rea_idx, dlc);

	(void)LIFO_Buffer_Clear(rx_buf);

	if (ctx->rea_buffer.pkt_cnt + 1 == ctx->seg_cnt) {
		ctx->rea_buffer.pkt_cnt = 0; // Reset packet count for next message
		ctx->curr_state = RX_DESERIALIZE;
		DEBUG_LOG_TRACE(TRANSPORT_TAG RX_FSM_TAG "[CH %d][L %d] [REASSEMBLE] All segments received. Transitioning to DESERIALIZE state. Segments count was: %d\n", ctx->channel_id, ctx->lane_id, ctx->seg_cnt);
	} else {
		// More segments to receive
		ctx->rea_buffer.pkt_cnt++;
		ctx->curr_state = RX_RECEIVE;

		DEBUG_LOG_TRACE(TRANSPORT_TAG RX_FSM_TAG "[CH %d][L %d] [REASSEMBLE] Segment received and reassembled. Current packet count: %d. Waiting for next segment. Segments count: %d\n", ctx->channel_id, ctx->lane_id,
						ctx->rea_buffer.pkt_cnt, ctx->seg_cnt);
	}
}

static inline void Rx_Deserialize_State(rx_context_t *ctx) {

	DEBUG_LOG_TRACE(TRANSPORT_TAG RX_FSM_TAG "[CH %d][L %d] [DESERIALIZE] Last RC: %s\n", ctx->channel_id, ctx->lane_id, RC_To_String(ctx->last_rc));

	ctx->last_rc = Deserialize_Object(ctx->rea_buffer.data, ctx->msg, ctx->msg_desc);
	if (ctx->last_rc == RC_OK) {
		ctx->curr_state = RX_IDLE;
		ctx->out_rc = RC_OK;

		if (ctx->msg_available_event) {
			DEBUG_LOG_ERROR(TRANSPORT_TAG RX_FSM_TAG "[CH %d][L %d] [DESERIALIZE] Message overrun_event detected. Previous message was not retrieved before receiving a new one.\n",
							ctx->channel_id, ctx->lane_id);
			ctx->overrun_event = true; // Set overrun_event flag if previous message was not retrieved
			ctx->abort_event = false;  // Clear abort_event flag if message was successfully deserialized but overrun_event occurred
		} else {
			ctx->msg_available_event = true;
			ctx->overrun_event = false; // Clear overrun_event flag if message was successfully deserialized and is available for retrieval
			ctx->abort_event = false;	// Clear abort_event flag if message was successfully deserialized and is available for retrieval
		}
		ctx->tag_available = true;
		
		// Metadata of the message just completed. The last segment was read by the
		// RECEIVE state immediately before REASSEMBLE -> DESERIALIZE, with no other
		// RECEIVE in between, so seg_timestamp_us is still the last segment's.
		// ctx->tag has been checked to be identical on every segment.
		ctx->msg_timestamp_us = ctx->seg_timestamp_us;
		ctx->msg_tag          = ctx->tag;

	} else {
		ctx->curr_state = RX_FAILED;
		ctx->out_rc = RC_FATAL;
		ctx->abort_event = true; // Set abort_event flag to indicate reception failure
	}
}

static inline void Rx_Failed_State(rx_context_t *ctx) {

	DEBUG_LOG_ERROR(TRANSPORT_TAG RX_FSM_TAG "[CH %d][L %d] [FAILED]. Last RC: %s\n", ctx->channel_id, ctx->lane_id, RC_To_String(ctx->last_rc));

	ctx->curr_state = RX_IDLE;
	ctx->out_rc = RC_FATAL;
	ctx->abort_event = true; // Set abort_event flag to indicate reception failure
	ctx->rea_buffer.pkt_cnt = 0;
}

static inline return_code_t Rx_FSM_Poll(uint8_t channel_key, uint8_t lane_id) {

	if (!Are_Channel_And_Lane_Valid(channel_key, lane_id)) {
		return RC_INVALID_ARG;
	}

	// Find the current context for the given channel and lane
	rx_context_t *ctx = &rx_ctx_table[channel_key][lane_id];

	switch (ctx->curr_state) {

	case RX_IDLE:
		Rx_Idle_State(ctx);
		break;
	case RX_WAIT:
		Rx_Wait_State(ctx);
		break;
	case RX_RECEIVE:
		Rx_Receive_State(ctx);
		break;
	case RX_REASSEMBLE:
		Rx_Reassemble_State(ctx);
		break;
	case RX_DESERIALIZE:
		Rx_Deserialize_State(ctx);
		break;
	case RX_FAILED:
		Rx_Failed_State(ctx);
		break;
	default:
		Rx_Failed_State(ctx);
		break;
	}

	DEBUG_LOG_TRACE(TRANSPORT_TAG RX_FSM_TAG "[CH %d][L %d] Polled RX FSM. Current State: %d, Last RC: %s, Msg Available Event: %d, Overrun Event: %d, Abort Event: %d\n",
					ctx->channel_id, ctx->lane_id, ctx->curr_state, RC_To_String(ctx->last_rc), ctx->msg_available_event, ctx->overrun_event, ctx->abort_event);

	return RC_OK;
}

/********************************************************************************************************************************
 * @par Transport Layer API Implementation
 ********************************************************************************************************************************/

return_code_t Transport_Init(const halo_node_t *node_desc) {

	DEBUG_LOG_TRACE(TRANSPORT_TAG "Initializing transport layer\n");

	if (node_desc == NULL) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG "NULL node descriptor passed to Transport_Init.\n");
		return RC_FATAL;
	}

	node = node_desc;

	return_code_t rc = Network_Init(node);
	if (rc != RC_OK) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG "Layer initialization failed. Network_Init failed. RC: %s\n", RC_To_String(rc));
		return RC_FATAL;
	}

	rc = Init_Msg_Desc_Lookup_Table();
	if (rc != RC_OK) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG "Layer initialization failed. Init_Msg_Desc_Lookup_Table failed. RC: %s\n", RC_To_String(rc));
		return RC_FATAL;
	}

	rc = Allocate_Msg_Buffers();
	if (rc != RC_OK) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG "Layer initialization failed. Allocate_Msg_Buffers failed. RC: %s\n", RC_To_String(rc));
		return RC_FATAL;
	}

	rc = Init_Tx_FSM();
	if (rc != RC_OK) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG "Layer initialization failed. Init_Tx_FSM failed. RC: %s\n", RC_To_String(rc));
		return RC_FATAL;
	}

	rc = Init_Rx_FSM();
	if (rc != RC_OK) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG "Layer initialization failed. Init_Rx_FSM failed. RC: %s\n", RC_To_String(rc));
		return RC_FATAL;
	}

	DEBUG_LOG_INFO(TRANSPORT_TAG "Layer initialized for Node ID: %d. RC: %s\n", node->id, RC_To_String(rc));

	return RC_OK;
}

return_code_t Transport_Buffer_Message(uint8_t channel_key, uint8_t lane_id, void *msg) {

	if (!Are_Channel_And_Lane_Valid(channel_key, lane_id) || msg == NULL) {
		return RC_INVALID_ARG;
	}

	tx_context_t *ctx = &tx_ctx_table[channel_key][lane_id];

	// If the TX FSM is busy serializing a message, a new message cannot be buffered until the current message
	// has been fully copied into the serialization buffer
	if (ctx->out_rc == RC_BUSY && ctx->curr_state == TX_SERIALIZE) {
		return RC_BUSY;
	} else {
		memcpy(ctx->msg, msg, ctx->msg_desc->object_size);
		ctx->msg_buffered_event = true;
		return RC_OK;
	}
}

return_code_t Transport_Send(uint8_t channel_key, uint8_t lane_id) {

	if (!Are_Channel_And_Lane_Valid(channel_key, lane_id)) {
		return RC_INVALID_ARG;
	}

	tx_context_t *ctx = &tx_ctx_table[channel_key][lane_id];

	if (ctx->out_rc == RC_BUSY) {
		return RC_BUSY;
	} else if (ctx->msg_buffered_event == false) {
		return RC_OK_NO_DATA;
	} else {
		(void)Tx_FSM_Poll(channel_key, lane_id, START_TX);
		ctx->msg_buffered_event = false;

		DEBUG_LOG_INFO(TRANSPORT_TAG TX_FSM_TAG "[CH %d][L %d] Started transmission.\n", ctx->channel_id, ctx->lane_id);

		return RC_OK;
	}
}

return_code_t Transport_Receive(uint8_t channel_key, uint8_t lane_id, void *msg) {

	if (!Are_Channel_And_Lane_Valid(channel_key, lane_id) || msg == NULL) {
		return RC_INVALID_ARG;
	}

	rx_context_t *ctx = &rx_ctx_table[channel_key][lane_id];

	if (ctx->msg_available_event) {						   // Check if a message is available and hasn't been retrieved yet
		// Save the application's descriptor pointer before copying
		object_desc_t *app_descriptor = *((object_desc_t **)msg);
		
		memcpy(msg, ctx->msg, ctx->msg_desc->object_size); // Copy deserialized message to output pointer
		
		// Restore the application's descriptor pointer (deserialization doesn't set this field)
		*((object_desc_t **)msg) = app_descriptor;
		
		ctx->msg_available_event = false;				   // Clear message available event after retrieval
		ctx->overrun_event = false;						   // Clear overrun_event flag after message retrieval
		ctx->abort_event = false;						   // Clear abort_event flag after message retrieval

		DEBUG_LOG_INFO(TRANSPORT_TAG RX_FSM_TAG "[CH %d][L %d] Message received.\n", ctx->channel_id, ctx->lane_id);

		return RC_OK; // Full message available
	} else {

		return RC_OK_NO_DATA; // No full message available
	}
}

return_code_t Transport_Receive_With_Meta(uint8_t channel_key, uint8_t lane_id, void *msg,
                                          uint32_t *rx_timestamp_us, uint8_t *tag_out) {

	if (!Are_Channel_And_Lane_Valid(channel_key, lane_id) || msg == NULL) {
		return RC_INVALID_ARG;
	}

	rx_context_t *ctx = &rx_ctx_table[channel_key][lane_id];

	if (!ctx->msg_available_event) {
		return RC_OK_NO_DATA;
	}

	// Read the metadata before Transport_Receive consumes the message.
	const uint32_t timestamp = ctx->msg_timestamp_us;
	const uint8_t  tag       = ctx->msg_tag;

	return_code_t rc = Transport_Receive(channel_key, lane_id, msg);
	if (rc == RC_OK) {
		if (rx_timestamp_us != NULL) {
			*rx_timestamp_us = timestamp;
		}
		if (tag_out != NULL) {
			*tag_out = tag;
		}
	}
	return rc;
}

/****************************************************************************************************************************
 * @par TX FSM Status and Event Query Functions
 ****************************************************************************************************************************/

return_code_t Transport_Tx_Is_Msg_Buffered(uint8_t channel_key, uint8_t lane_id) {

	if (!Are_Channel_And_Lane_Valid(channel_key, lane_id)) {
		return RC_INVALID_ARG;
	}
	tx_context_t *ctx = &tx_ctx_table[channel_key][lane_id];
	if (ctx->msg_buffered_event) {
		return RC_OK_TRUE;
	} else {
		return RC_OK_FALSE;
	}
}

return_code_t Transport_Tx_Is_Abort(uint8_t channel_key, uint8_t lane_id) {

	if (!Are_Channel_And_Lane_Valid(channel_key, lane_id)) {
		return RC_INVALID_ARG;
	}
	tx_context_t *ctx = &tx_ctx_table[channel_key][lane_id];
	if (ctx->abort_event) {
		ctx->abort_event = false; // Clear abort_event flag after querying
		return RC_OK_TRUE;
	} else {
		return RC_OK_FALSE;
	}
}

return_code_t Transport_Tx_Is_Done(uint8_t channel_key, uint8_t lane_id) {

	if (!Are_Channel_And_Lane_Valid(channel_key, lane_id)) {
		return RC_INVALID_ARG;
	}
	tx_context_t *ctx = &tx_ctx_table[channel_key][lane_id];

	if (ctx->done_event) {
		ctx->done_event = false; // Clear done_event flag after querying
		return RC_OK_TRUE;
	} else {
		return RC_OK_FALSE;
	}
}

return_code_t Transport_Tx_Is_Busy(uint8_t channel_key, uint8_t lane_id) {

	if (!Are_Channel_And_Lane_Valid(channel_key, lane_id)) {
		return RC_INVALID_ARG;
	}
	tx_context_t *ctx = &tx_ctx_table[channel_key][lane_id];
	if (ctx->out_rc == RC_BUSY) {
		return RC_OK_TRUE;
	} else {
		return RC_OK_FALSE;
	}
}

return_code_t Transport_Tx_Get_Status(uint8_t channel_key, uint8_t lane_id) {

	if (!Are_Channel_And_Lane_Valid(channel_key, lane_id)) {
		return RC_INVALID_ARG;
	}
	tx_context_t *ctx = &tx_ctx_table[channel_key][lane_id];
	return ctx->out_rc;
}

return_code_t Transport_Tx_Get_Error(uint8_t channel_key, uint8_t lane_id) {

	if (!Are_Channel_And_Lane_Valid(channel_key, lane_id)) {
		return RC_INVALID_ARG;
	}
	tx_context_t *ctx = &tx_ctx_table[channel_key][lane_id];

	return ctx->last_rc;
}

/*********************************************************************************************************************
 * @par RX FSM Status and Event Query Functions
 ********************************************************************************************************************/

return_code_t Transport_Rx_Get_Status(uint8_t channel_key, uint8_t lane_id) {

	if (!Are_Channel_And_Lane_Valid(channel_key, lane_id)) {
		return RC_INVALID_ARG;
	}
	rx_context_t *ctx = &rx_ctx_table[channel_key][lane_id];
	return ctx->out_rc;
}

return_code_t Transport_Rx_Get_Error(uint8_t channel_key, uint8_t lane_id) {

	if (!Are_Channel_And_Lane_Valid(channel_key, lane_id)) {
		return RC_INVALID_ARG;
	}
	rx_context_t *ctx = &rx_ctx_table[channel_key][lane_id];

	return ctx->last_rc;
}

return_code_t Transport_Rx_Is_Busy(uint8_t channel_key, uint8_t lane_id) {

	if (!Are_Channel_And_Lane_Valid(channel_key, lane_id)) {
		return RC_INVALID_ARG;
	}
	rx_context_t *ctx = &rx_ctx_table[channel_key][lane_id];
	if (ctx->out_rc == RC_BUSY) {
		return RC_OK_TRUE;
	} else {
		return RC_OK_FALSE;
	}
}

return_code_t Transport_Rx_Is_Msg_Available(uint8_t channel_key, uint8_t lane_id) {

	if (!Are_Channel_And_Lane_Valid(channel_key, lane_id)) {
		DEBUG_LOG_ERROR(TRANSPORT_TAG "[Transport_Rx_Is_Msg_Available] Invalid channel key (%d) or lane ID (%d) passed to Transport_Rx_Is_Msg_Available.\n", channel_key, lane_id);
		return RC_INVALID_ARG;
	}
	rx_context_t *ctx = &rx_ctx_table[channel_key][lane_id];
	if (ctx->msg_available_event) {
		return RC_OK_TRUE;
	} else {
		return RC_OK_FALSE;
	}
}

return_code_t Transport_Rx_Is_Overrun(uint8_t channel_key, uint8_t lane_id) {

	if (!Are_Channel_And_Lane_Valid(channel_key, lane_id)) {
		return RC_INVALID_ARG;
	}
	rx_context_t *ctx = &rx_ctx_table[channel_key][lane_id];

	if (ctx->overrun_event) {
		ctx->overrun_event = false; // Clear overrun_event flag after querying
		return RC_OK_TRUE;
	} else {
		return RC_OK_FALSE;
	}
}

return_code_t Transport_Rx_Is_Abort(uint8_t channel_key, uint8_t lane_id) {

	if (!Are_Channel_And_Lane_Valid(channel_key, lane_id)) {
		return RC_INVALID_ARG;
	}
	rx_context_t *ctx = &rx_ctx_table[channel_key][lane_id];

	if (ctx->abort_event) {
		ctx->abort_event = false; // Clear abort_event flag after querying
		return RC_OK_TRUE;

	} else {
		return RC_OK_FALSE;
	}
}

return_code_t Transport_Poll(uint8_t channel_key, uint8_t lane_id) {

	if (!Are_Channel_And_Lane_Valid(channel_key, lane_id)) {
		return RC_INVALID_ARG;
	}
	(void)Network_Poll();
	(void)Tx_FSM_Poll(channel_key, lane_id, POLL_TX);
	(void)Rx_FSM_Poll(channel_key, lane_id);

	return RC_OK;
}