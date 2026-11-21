#include "../Private/Application.h"

#include "../../Config/Include/Config.h"
#include "../../PAL/Private/PAL_CAN_Config.h"
#include <stdlib.h>
#include <string.h>
/**************************************************** DEFINES ***************************************** */

//#define APP_TARGET_NODE HALO_NODE_PORTENTA
//#define APP_NUM_CHANNELS NUM_CHANNELS
#define APP_NUM_LANES 2
//#define APP_TARGET_NODE_NUM_CTRL_ENDPOINTS 1
//#define APP_TARGET_NODE_NUM_RESP_ENDPOINTS (APP_NUM_CHANNELS - APP_TARGET_NODE_NUM_CTRL_ENDPOINTS)
#define APP_LANE0 0
#define APP_LANE1 1
#define APP_CONTROLLER_REQUEST_BUDGET 1
#define APP_CONTROLLER_SEND_BUDGET 1

#define APP_CTRL_L0_BUFFER_REQUEST true
#define APP_CTRL_L0_POLL false
#define APP_RESP_L0_BUFFER_RESPONSE true
#define APP_RESP_L0_POLL false
#define APPLICATION_TAG "[APP] "

static const halo_node_t *node = NULL;

// RICORDATI DI IMPLEMENTARE LA FRESHNESS

/**************************************************** HELPERS ***************************************** */

static inline return_code_t Is_Node_Controller_For_Channel(const halo_channel_t *channel) {
	if (channel->controller->id == node->id) {
		return RC_OK_TRUE;
	} else {
		return RC_OK_FALSE;
	}
}

static inline return_code_t Is_Node_Responder_For_Channel(const halo_channel_t *channel) {
	if (channel->responder->id == node->id) {
		return RC_OK_TRUE;
	} else {
		return RC_OK_FALSE;
	}
}

static inline return_code_t Is_Node_Listener_For_Channel(const halo_channel_t *channel) {
	if (channel->controller->id != node->id && channel->responder->id != node->id) {
		return RC_OK_TRUE;
	}
	return RC_OK_FALSE;
}

static inline int16_t Get_Listener_Channel_Key_From_Channel_Key(uint8_t channel_key) {
	if (channel_key >= node->num_channels) {
		return -1;
	}
	if (Is_Node_Listener_For_Channel(node->channels[channel_key]) != RC_OK_TRUE) {
		return -1;
	}
	uint8_t k = 0;
	for (uint8_t i = 0; i < channel_key; i++) {
		if (Is_Node_Listener_For_Channel(node->channels[i]) == RC_OK_TRUE) {
			k++;
		}
	}
	return k;
}

static inline return_code_t Is_Channel_Valid(const halo_channel_t *channel) {
	if (channel == NULL) {
		return RC_OK_FALSE;
	}
	return RC_OK_TRUE;
}

static inline return_code_t Does_Msg_Match_Channel_And_Lane(const halo_channel_t *channel, uint8_t lane_id, void *msg) {

	const msg_desc_t *expected_msg_desc = NULL;
	if (lane_id == APP_LANE0) {
		expected_msg_desc = channel->lane0_message;
	} else if (lane_id == APP_LANE1) {
		expected_msg_desc = channel->lane1_message;
	} else {
		return RC_INVALID_ARG; // Invalid lane ID
	}

	if (expected_msg_desc == NULL) {
		return RC_INVALID_ARG; // No message configured for this lane
	}

	if (msg == NULL) {
		return RC_INVALID_ARG; // NULL message pointer
	}

	object_desc_t *msg_obj_desc = *((object_desc_t **)msg); // Get the object descriptor pointer from the message header

	if (msg_obj_desc != expected_msg_desc) {
		return RC_OK_FALSE; // Message descriptor does not match expected descriptor for this channel and lane
	}

	return RC_OK_TRUE;
}

static inline int16_t Get_Channel_Key_From_ID(uint16_t channel_id) {
	for (uint8_t i = 0; i < node->num_channels; i++) {
		if (node->channels[i]->global_id == channel_id) {
			return i;
		}
	}
	return -1;
}

static inline int16_t Get_Channel_ID_From_Channel_Key(uint8_t channel_key) {
	if (channel_key >= node->num_channels) {
		return -1;
	}
	return node->channels[channel_key]->global_id;
}

static inline int16_t Get_Controller_Channel_Key_From_Channel_Key(uint8_t channel_key) {

	if (channel_key >= node->num_channels) {
		return -1;
	}

	if (node->channels[channel_key]->controller->id != node->id) {
		return -1; // Not a controller channel
	}

	uint8_t ctrl_channel_key = 0;
	for (uint8_t i = 0; i < channel_key; i++) {
		if (node->channels[i]->controller->id == node->id) {
			ctrl_channel_key++;
		}
	}
	return ctrl_channel_key;
}

static inline int16_t Get_Responder_Channel_Key_From_Channel_Key(uint8_t channel_key) {

	if (channel_key >= node->num_channels) {
		return -1;
	}

	if (node->channels[channel_key]->responder->id != node->id) {
		return -1; // Not a responder channel
	}

	uint8_t resp_channel_key = 0;
	for (uint8_t i = 0; i < channel_key; i++) {
		if (node->channels[i]->responder->id == node->id) {
			resp_channel_key++;
		}
	}
	return resp_channel_key;
}

/*********************************************************************************************************************************
 * @par Controller Endpoint Implementation
 *
 * @details
 *
 *
 *
 *
 *
 *********************************************************************************************************************************/

/*********************************************************************************************************************************
 * @par Shared Data Structures
 *********************************************************************************************************************************/
static uint8_t outstanding_req_cnt = 0;

// Round coordinator gate (defined after the L1 FSM context, below). True while
// any controller lane-1 FSM is waiting for the current request round to drain.
// Ctrl_L0_Idle_State consults it to defer opening a new round until lane-1 has
// been released, making lane-1's per-round send window a protocol guarantee
// rather than an assumption on the caller's request cadence. Conditional by
// construction: only true when lane-1 actually has a message in flight.
static bool Lane1_Round_Window_Owed(void);




/********************************************************************************************************************************
 * @par Listener FSM Implementation
 ********************************************************************************************************************************/
typedef enum { LST_IDLE, LST_WAIT_FOR_MSG, LST_FAILED } listener_state_t;

typedef struct {
	listener_state_t curr_state;
	uint16_t channel_id;
	uint8_t channel_key;
	return_code_t last_rc;
	bool msg_available_event;
	bool abort_event;
} listener_context_t;

static listener_context_t *lst_ctx = NULL; // [num_listener_endpoints * APP_NUM_LANES]
static uint8_t num_listener_endpoints = 0;

static inline return_code_t Init_Listener_FSM(void) {

	num_listener_endpoints = 0;
	for (uint8_t i = 0; i < node->num_channels; i++) {
		if (Is_Node_Listener_For_Channel(node->channels[i]) == RC_OK_TRUE) {
			num_listener_endpoints++;
		}
	}
	if (num_listener_endpoints == 0) {
		return RC_OK; // nessuna allocazione per nodi non listener
	}

	lst_ctx = (listener_context_t *)calloc((size_t)num_listener_endpoints * APP_NUM_LANES, sizeof(listener_context_t));
	if (lst_ctx == NULL) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[LST] Failed to allocate Listener FSM contexts.\n");
		return RC_FATAL;
	}

	uint8_t k = 0;
	for (uint8_t i = 0; i < node->num_channels; i++) {
		if (Is_Node_Listener_For_Channel(node->channels[i]) != RC_OK_TRUE) {
			continue;
		}
		for (uint8_t lane = 0; lane < APP_NUM_LANES; lane++) {
			listener_context_t *ctx = &lst_ctx[k * APP_NUM_LANES + lane];
			ctx->curr_state = LST_IDLE;
			ctx->channel_id = node->channels[i]->global_id;
			ctx->channel_key = i;
			ctx->last_rc = RC_OK;
			ctx->msg_available_event = false;
			ctx->abort_event = false;
		}
		k++;
	}
	return RC_OK;
}

static inline void Listener_FSM_Poll(uint8_t lst_key, uint8_t lane_id) {

	listener_context_t *ctx = &lst_ctx[lst_key * APP_NUM_LANES + lane_id];

	switch (ctx->curr_state) {
	case LST_IDLE:
		ctx->curr_state = LST_WAIT_FOR_MSG;
		break;
	case LST_WAIT_FOR_MSG:
		if (ctx->msg_available_event) {
			break; // messaggio precedente non ancora consumato
		}
		ctx->last_rc = Transport_Rx_Is_Msg_Available(ctx->channel_key, lane_id);
		if (ctx->last_rc == RC_OK_TRUE) {
			ctx->msg_available_event = true;
		} else if (ctx->last_rc != RC_OK_FALSE) {
			ctx->curr_state = LST_FAILED;
		}
		break;
	case LST_FAILED:
	default:
		ctx->abort_event = true;
		ctx->curr_state = LST_IDLE;
		break;
	}
}

static inline return_code_t Listener_Resolve(const halo_channel_t *channel, uint8_t lane_id,
                                             listener_context_t **ctx_out, int16_t *channel_key_out) {
	if (Is_Channel_Valid(channel) != RC_OK_TRUE) return RC_INTEGRITY_FAIL;
	if (lane_id >= APP_NUM_LANES)                return RC_INVALID_ARG;
	if (Is_Node_Listener_For_Channel(channel) != RC_OK_TRUE) return RC_PERM_DENIED;

	int16_t channel_key = Get_Channel_Key_From_ID(channel->global_id);
	if (channel_key < 0) return RC_INTEGRITY_FAIL;

	int16_t lst_key = Get_Listener_Channel_Key_From_Channel_Key((uint8_t)channel_key);
	if (lst_key < 0 || lst_ctx == NULL) return RC_INTEGRITY_FAIL;

	*ctx_out = &lst_ctx[lst_key * APP_NUM_LANES + lane_id];
	*channel_key_out = channel_key;
	return RC_OK;
}

return_code_t Listener_Is_Msg_Available(const halo_channel_t *channel, uint8_t lane_id) {
	listener_context_t *ctx = NULL;
	int16_t channel_key = -1;
	return_code_t rc = Listener_Resolve(channel, lane_id, &ctx, &channel_key);
	if (rc != RC_OK) return rc;
	return ctx->msg_available_event ? RC_OK_TRUE : RC_OK_FALSE;
}

// msg va inizializzato con la INIT del tipo atteso (il descriptor è nel primo campo)
return_code_t Listener_Receive(const halo_channel_t *channel, uint8_t lane_id, void *msg,
                               uint32_t *timestamp_out, uint8_t *tag_out) {

	listener_context_t *ctx = NULL;
	int16_t channel_key = -1;
	return_code_t rc = Listener_Resolve(channel, lane_id, &ctx, &channel_key);
	if (rc != RC_OK) return rc;

	if (Does_Msg_Match_Channel_And_Lane(channel, lane_id, msg) != RC_OK_TRUE) return RC_INTEGRITY_FAIL;

	if (ctx->abort_event) {
		ctx->abort_event = false;
		return RC_FATAL;
	}
	if (!ctx->msg_available_event) return RC_OK_NO_DATA;

	// Message, timestamp and tag come from the Transport in one call: they always
	// belong to the same message.
	rc = Transport_Receive_With_Meta((uint8_t)channel_key, lane_id, msg, timestamp_out, tag_out);
	ctx->msg_available_event = false;

	if (rc == RC_OK)         return RC_OK;
	if (rc == RC_OK_NO_DATA) return RC_OK_NO_DATA;
	return RC_ERROR;
}

// Lane 0 requests are single-frame events: the listener's Transport discards them, so they
// are read from the Network request queue (arrival timestamp + tag of the request frame).
return_code_t Listener_Receive_Request_With_Tag(const halo_channel_t *channel,
                                                uint32_t *timestamp_out,
                                                uint8_t *tag_out) {
	if (timestamp_out == NULL || tag_out == NULL) return RC_INVALID_ARG;

	listener_context_t *ctx = NULL;
	int16_t channel_key = -1;
	return_code_t rc = Listener_Resolve(channel, APP_LANE0, &ctx, &channel_key);
	if (rc != RC_OK) return rc;

	return Network_Get_Request_Event((uint8_t)channel_key, timestamp_out, tag_out);
}




return_code_t Listener_Get_Controller_Time(const halo_channel_t *channel, uint32_t *time_out) {
	if (Is_Channel_Valid(channel) != RC_OK_TRUE) return RC_INTEGRITY_FAIL;
	if (time_out == NULL)                        return RC_INVALID_ARG;
	if (Is_Node_Listener_For_Channel(channel) != RC_OK_TRUE) return RC_PERM_DENIED;
	return Network_Get_Controller_Time((uint8_t)channel->controller->id, time_out);
}

return_code_t Listener_Take_Unknown_Ids(pal_unknown_id_t *out, uint8_t max,
                                        uint8_t *count, uint32_t *untracked_frames) {

	if (out == NULL || count == NULL || untracked_frames == NULL) return RC_INVALID_ARG;
	if (node == NULL)                                             return RC_FATAL;

	// Solo un listener puro traccia gli ID esterni (stessa condizione di passive_only nella PAL).
	if (node->num_controller_endpoints != 0 || node->num_responder_endpoints != 0 ||
	    num_listener_endpoints == 0) {
		*count = 0;
		*untracked_frames = 0;
		return RC_PERM_DENIED;
	}

	return PAL_Take_Unknown_Ids(out, max, count, untracked_frames);
}




/********************************************************************************************************************************
 * @par Controller L0 FSM Implementation
 ********************************************************************************************************************************/
typedef enum { CTRL_L0_IDLE, CTRL_L0_SEND_REQUEST, CTRL_L0_WAIT_FOR_RESPONSE, CTRL_L0_WAIT_FOR_ROUND, CTRL_L0_FAILED } controller_l0_state_t;

typedef struct {
	controller_l0_state_t curr_state;
	uint16_t channel_id;
	uint8_t channel_key;
	uint64_t timeout;
	uint64_t waiting_since;
	return_code_t last_rc;
	return_code_t out_rc;
	bool tx_in_progress;
	bool resp_available_event;
	bool timeout_event;
	bool abort_event;
	bool resp_ack_event; // Indicates whether the controller has acknowledged the response
	uint8_t next_request_tag;
	uint8_t request_tag;
} controller_l0_context_t;

static controller_l0_context_t *ctrl_l0_ctx = NULL;

static inline return_code_t Init_Ctrl_L0_FSM(void) {

	DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [Init_Ctrl_L0_FSM] Initializing Controller L0 FSM\n");

	uint8_t ctrl_channel_key = 0;
	ctrl_l0_ctx = malloc(node->num_controller_endpoints * sizeof(controller_l0_context_t));

	const halo_channel_t *const *channels = node->channels;

	for (uint8_t i = 0; i < node->num_channels; i++) {
		if (channels[i]->controller->id == node->id) { // This is a controller channel, initialize controller FSM context for lane 0
			// This is a controller channel, initialize controller FSM context for lane 0
			ctrl_l0_ctx[ctrl_channel_key].curr_state = CTRL_L0_IDLE;
			ctrl_l0_ctx[ctrl_channel_key].channel_id = channels[i]->global_id;
			ctrl_l0_ctx[ctrl_channel_key].channel_key = i;
			ctrl_l0_ctx[ctrl_channel_key].last_rc = RC_OK;
			ctrl_l0_ctx[ctrl_channel_key].out_rc = RC_OK;
			ctrl_l0_ctx[ctrl_channel_key].resp_available_event = false;
			ctrl_l0_ctx[ctrl_channel_key].timeout_event = false;
			ctrl_l0_ctx[ctrl_channel_key].abort_event = false;
			ctrl_l0_ctx[ctrl_channel_key].resp_ack_event = true;
			ctrl_l0_ctx[ctrl_channel_key].tx_in_progress = false;
			ctrl_l0_ctx[ctrl_channel_key].next_request_tag = 0;
			ctrl_l0_ctx[ctrl_channel_key].request_tag = 0;
			ctrl_l0_ctx[ctrl_channel_key].timeout = channels[i]->lane0_rx_timeout;
			ctrl_l0_ctx[ctrl_channel_key].waiting_since = 0;

			ctrl_channel_key++;

			DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [Init_Ctrl_L0_FSM] Initialized Controller L0 FSM for Channel Key: %d (Channel ID: %d)\n",
							ctrl_l0_ctx[ctrl_channel_key - 1].channel_key, channels[i]->global_id);
		}
	}

	if (ctrl_channel_key != node->num_controller_endpoints) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [Init_Ctrl_L0_FSM] Mismatch in expected number of controller channels. Expected: %d, Found: %d\n", node->num_controller_endpoints,
						ctrl_channel_key);
		return RC_INVALID_ARG;
	} else {
		DEBUG_LOG_INFO(APPLICATION_TAG "[CTRL] [Init_Ctrl_L0_FSM] Successfully initialized Controller L0 FSM for all controller channels. Total Controller Channels: %d\n",
					   node->num_controller_endpoints);
		return RC_OK;
	}
}

static inline void Ctrl_L0_Idle_State(controller_l0_context_t *ctx, bool request) {

	// Defer opening a new round while lane-1 is still waiting for the current one
	// to drain (round coordinator). This freezes new request issuance during the
	// drain, so the outstanding set can only shrink -> the count is guaranteed to
	// reach 0 and release lane-1 before a new round can re-block it.
	if (request && ctx->resp_ack_event && !Lane1_Round_Window_Owed()) {

		halo_request_message_t req_msg = HALO_REQUEST_MESSAGE_INIT;
		uint8_t request_tag = ctx->next_request_tag;
		ctx->last_rc = Transport_Tx_Set_Tag(ctx->channel_key, APP_LANE0, request_tag);
		if (ctx->last_rc == RC_OK) {
			ctx->last_rc = Transport_Buffer_Message(ctx->channel_key, APP_LANE0, &req_msg);
		}
		if (ctx->last_rc != RC_OK) {

			ctx->curr_state = CTRL_L0_FAILED;

			DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [IDLE] Failed to prepare tagged request. RC: %s\n", ctx->channel_id, APP_LANE0, RC_To_String(ctx->last_rc));

		} else {

			ctx->request_tag = request_tag;
			ctx->next_request_tag = (uint8_t)((request_tag + 1U) & CAN_TAG_MASK);
			ctx->curr_state = CTRL_L0_SEND_REQUEST;
			ctx->last_rc = RC_OK;
			ctx->out_rc = RC_BUSY;
			ctx->resp_available_event = false;
			ctx->timeout_event = false;
			ctx->abort_event = false;
			ctx->resp_ack_event = false;
			ctx->tx_in_progress = false;
			outstanding_req_cnt++;

			DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [IDLE] Successfully buffered request message\n", ctx->channel_id, APP_LANE0);
		}

	} else {

		ctx->curr_state = CTRL_L0_IDLE;
		ctx->out_rc = RC_OK;
	}
}

static inline void Ctrl_L0_Send_Request_State(controller_l0_context_t *ctx) {

	DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [SEND_REQUEST] Attempting to send request message\n", ctx->channel_id, APP_LANE0);

	if (!ctx->tx_in_progress) {

		ctx->last_rc = Transport_Send(ctx->channel_key, APP_LANE0);
		if (ctx->last_rc == RC_OK) {

			ctx->tx_in_progress = true;
			ctx->curr_state = CTRL_L0_SEND_REQUEST;

			DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [SEND_REQUEST] Request message transmission started.\n", ctx->channel_id, APP_LANE0);

		} else {

			ctx->curr_state = CTRL_L0_FAILED;
			outstanding_req_cnt--;

			DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [SEND_REQUEST] Failed to send request message. RC: %s\n", ctx->channel_id, APP_LANE0, RC_To_String(ctx->last_rc));
		}

	} else {

		return_code_t done_event = Transport_Tx_Is_Done(ctx->channel_key, APP_LANE0);
		return_code_t abort_event = Transport_Tx_Is_Abort(ctx->channel_key, APP_LANE0);

		if (done_event == RC_OK_TRUE && abort_event == RC_OK_FALSE) {

			ctx->tx_in_progress = false;
			ctx->last_rc = RC_OK;
			ctx->curr_state = CTRL_L0_WAIT_FOR_RESPONSE;
			ctx->waiting_since = HAL_Get_Micros();

			DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [SEND_REQUEST] Request message transmission completed.\n", ctx->channel_id, APP_LANE0);

		} else if (done_event == RC_OK_FALSE && abort_event == RC_OK_FALSE) {

			ctx->curr_state = CTRL_L0_SEND_REQUEST; // Keep waiting for transmission to complete

			DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [SEND_REQUEST] Request message transmission in progress. Waiting for completion.\n", ctx->channel_id, APP_LANE0);

		} else {

			ctx->tx_in_progress = false;
			ctx->last_rc = RC_ERROR;
			ctx->curr_state = CTRL_L0_FAILED;
			outstanding_req_cnt--;

			DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [SEND_REQUEST] Request message transmission failed. RC: %s\n", ctx->channel_id, APP_LANE0,
							RC_To_String(ctx->last_rc));
		}
	}
}

static inline void Ctrl_L0_Wait_For_Response_State(controller_l0_context_t *ctx) {

	DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [WAIT_FOR_RESPONSE] Waiting for response message\n", ctx->channel_id, APP_LANE0);

	uint64_t wait_time = HAL_Get_Micros() - ctx->waiting_since; // Update elapsed wait time

	if (ctx->timeout != 0 && wait_time >= ctx->timeout) {

		ctx->curr_state = CTRL_L0_FAILED;
		ctx->last_rc = RC_TIMEOUT;
		ctx->timeout_event = true;
		ctx->resp_available_event = false;
		outstanding_req_cnt--;

		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [WAIT_FOR_RESPONSE] Response timed out after %llu microseconds.\n", ctx->channel_id, APP_LANE0, wait_time);
		(void)Transport_Rx_Reset(ctx->channel_key, APP_LANE0); // Reset RX FSM to clear any partial state from the timed-out response

		return;
	}

	return_code_t rx_abort = Transport_Rx_Is_Abort(ctx->channel_key, APP_LANE0);
	if (rx_abort == RC_OK_TRUE) {
		return_code_t rx_error = Transport_Rx_Get_Error(ctx->channel_key, APP_LANE0);
		return_code_t reset_rc = Transport_Rx_Reset(ctx->channel_key, APP_LANE0);

		ctx->curr_state = CTRL_L0_FAILED;
		ctx->last_rc = rx_error;
		outstanding_req_cnt--;

		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [WAIT_FOR_RESPONSE] Response reception aborted for request tag %u. Transport RC: %s\n",
						ctx->channel_id, APP_LANE0, ctx->request_tag, RC_To_String(rx_error));
		if (reset_rc != RC_OK) {
			DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [WAIT_FOR_RESPONSE] Failed to reset RX after reception abort. RC: %s\n",
							ctx->channel_id, APP_LANE0, RC_To_String(reset_rc));
		}
		return;
	} else if (rx_abort != RC_OK_FALSE) {
		return_code_t reset_rc = Transport_Rx_Reset(ctx->channel_key, APP_LANE0);

		ctx->curr_state = CTRL_L0_FAILED;
		ctx->last_rc = rx_abort;
		outstanding_req_cnt--;

		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [WAIT_FOR_RESPONSE] Failed to check RX abort status. RC: %s\n",
						ctx->channel_id, APP_LANE0, RC_To_String(rx_abort));
		if (reset_rc != RC_OK) {
			DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [WAIT_FOR_RESPONSE] Failed to reset RX after abort-status error. RC: %s\n",
							ctx->channel_id, APP_LANE0, RC_To_String(reset_rc));
		}
		return;
	}

	ctx->last_rc = Transport_Rx_Is_Msg_Available(ctx->channel_key, APP_LANE0);

	if (ctx->last_rc == RC_OK_TRUE) {

		uint8_t response_tag = 0;
		return_code_t tag_rc = Transport_Rx_Get_Tag(ctx->channel_key, APP_LANE0, &response_tag);
		if (tag_rc != RC_OK) {
			ctx->curr_state = CTRL_L0_FAILED;
			outstanding_req_cnt--;
			DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [WAIT_FOR_RESPONSE] Failed to read response tag. RC: %s\n",
							ctx->channel_id, APP_LANE0, RC_To_String(tag_rc));
			return;
		}

		if (response_tag != ctx->request_tag) {
			DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [WAIT_FOR_RESPONSE] Discarding response with stale tag %u; expected %u.\n",
							ctx->channel_id, APP_LANE0, response_tag, ctx->request_tag);
			ctx->last_rc = Transport_Rx_Reset(ctx->channel_key, APP_LANE0);
			if (ctx->last_rc != RC_OK) {
				ctx->curr_state = CTRL_L0_FAILED;
				outstanding_req_cnt--;
				DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [WAIT_FOR_RESPONSE] Failed to reset RX after stale response. RC: %s\n",
								ctx->channel_id, APP_LANE0, RC_To_String(ctx->last_rc));
			}
			return;
		}

		ctx->curr_state = CTRL_L0_WAIT_FOR_ROUND;
		ctx->resp_available_event = true;
		outstanding_req_cnt--;

		DEBUG_LOG_INFO(APPLICATION_TAG "[CTRL] [CH %d][L %d] [WAIT_FOR_RESPONSE] Response received\n", ctx->channel_id, APP_LANE0);

	} else if (ctx->last_rc == RC_OK_FALSE) {

		ctx->curr_state = CTRL_L0_WAIT_FOR_RESPONSE; // Keep waiting

	} else {

		ctx->curr_state = CTRL_L0_FAILED;
		outstanding_req_cnt--;

		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [WAIT_FOR_RESPONSE] Error while checking for response availability. RC: %s\n", ctx->channel_id, APP_LANE0,
						RC_To_String(ctx->last_rc));
	}
}

static inline void Ctrl_L0_Wait_For_Round_State(controller_l0_context_t *ctx) {

	DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [WAIT_FOR_ROUND] Waiting for round to complete\n", ctx->channel_id, APP_LANE0);

	if (outstanding_req_cnt == 0) {

		ctx->curr_state = CTRL_L0_IDLE;
		ctx->last_rc = RC_OK;
		ctx->out_rc = RC_OK;

		DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [WAIT_FOR_ROUND] Round completed. Returning to IDLE state.\n", ctx->channel_id, APP_LANE0);

	} else if (outstanding_req_cnt > 0) {

		ctx->last_rc = RC_OK;
		ctx->curr_state = CTRL_L0_WAIT_FOR_ROUND; // Keep waiting for round to complete

		DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [WAIT_FOR_ROUND] Waiting for round to complete. Outstanding requests: %d\n", ctx->channel_id, APP_LANE0,
						outstanding_req_cnt);

	} else {

		ctx->curr_state = CTRL_L0_FAILED;
		ctx->last_rc = RC_ERROR;

		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [WAIT_FOR_ROUND] Invalid outstanding request count: %d\n", ctx->channel_id, APP_LANE0, outstanding_req_cnt);
	}
}

static inline void Ctrl_L0_Failed_State(controller_l0_context_t *ctx) {

	DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [FAILED] Operation failed.\n", ctx->channel_id, APP_LANE0);

	ctx->out_rc = RC_FATAL;
	ctx->abort_event = true; // Set abort_event flag to indicate failure

	if (outstanding_req_cnt > 0) {
		ctx->curr_state = CTRL_L0_WAIT_FOR_ROUND; // Wait for round to complete to ensure system stability before returning to IDLE
		DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [FAILED] Can't return to IDLE. Outstanding requests: %d. Moving to WAIT_FOR_ROUND state.\n", ctx->channel_id,
						APP_LANE0, outstanding_req_cnt);
	} else if (outstanding_req_cnt == 0) {
		ctx->curr_state = CTRL_L0_IDLE; // No outstanding requests, can return to IDLE immediately
		DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [FAILED] No outstanding requests. Returning to IDLE.\n", ctx->channel_id, APP_LANE0);
	} else {
		ctx->curr_state = CTRL_L0_FAILED; // Invalid outstanding request count, stay in FAILED state
		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [FAILED] Invalid outstanding request count: %d\n", ctx->channel_id, APP_LANE0, outstanding_req_cnt);
	}
}

return_code_t Controller_L0_FSM_Poll(uint8_t ctrl_channel_key, bool request) {

	controller_l0_context_t *ctx = &ctrl_l0_ctx[ctrl_channel_key];

	switch (ctx->curr_state) {
	case CTRL_L0_IDLE:
		Ctrl_L0_Idle_State(ctx, request);
		break;
	case CTRL_L0_SEND_REQUEST:
		Ctrl_L0_Send_Request_State(ctx);
		break;
	case CTRL_L0_WAIT_FOR_RESPONSE:
		Ctrl_L0_Wait_For_Response_State(ctx);
		break;
	case CTRL_L0_WAIT_FOR_ROUND:
		Ctrl_L0_Wait_For_Round_State(ctx);
		break;
	case CTRL_L0_FAILED:
		Ctrl_L0_Failed_State(ctx);
		break;
	default:
		Ctrl_L0_Failed_State(ctx);
		break;
	}

	return RC_OK;
}

/********************************************************************************************************************************
 * @par Controller L1 FSM Implementation
 ********************************************************************************************************************************/
typedef enum { CTRL_L1_IDLE, CTRL_L1_SEND_MESSAGE, CTRL_L1_WAIT_FOR_ROUND, CTRL_L1_FAILED } controller_l1_state_t;

typedef struct {
	controller_l1_state_t curr_state;
	uint16_t channel_id;
	uint8_t channel_key;
	return_code_t last_rc;
	return_code_t out_rc;
	bool tx_in_progress;
	bool abort_event;
	bool done_event; // Indicates whether the controller has acknowledged the response
} controller_l1_context_t;

static controller_l1_context_t *ctrl_l1_ctx = NULL;

// Round coordinator (forward-declared near outstanding_req_cnt). A lane-1 FSM
// sits in CTRL_L1_WAIT_FOR_ROUND only after it has transmitted its message and
// is blocked until the round drains. While any lane-1 FSM is in that state,
// Ctrl_L0_Idle_State holds off new requests, guaranteeing the count reaches 0
// and releases lane-1 before a new round can re-block it.
//
// Symmetric / no needless capping:
//   - No requests outstanding -> lane-1's own WAIT_FOR_ROUND clears immediately
//     at count==0, so lane-1 runs at full rate.
//   - No lane-1 traffic       -> no FSM is ever in WAIT_FOR_ROUND, so this
//     returns false and request issuance is never gated.
// The round/lane-1 alternation is therefore imposed only under genuine
// contention; the worst-case bound (both active) is unchanged.
static bool Lane1_Round_Window_Owed(void) {
	if (ctrl_l1_ctx == NULL) {
		return false; // No controller endpoints (or not yet initialised)
	}
	for (uint8_t i = 0; i < node->num_controller_endpoints; i++) {
		if (ctrl_l1_ctx[i].curr_state == CTRL_L1_WAIT_FOR_ROUND) {
			return true;
		}
	}
	return false;
}

static inline return_code_t Init_Ctrl_L1_FSM(void) {

	DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [Init_Ctrl_L1_FSM] Initializing Controller L1 FSM\n");

	uint8_t ctrl_channel_key = 0;
	ctrl_l1_ctx = malloc(node->num_controller_endpoints * sizeof(controller_l1_context_t));
	const halo_channel_t *const *channels = node->channels;
	for (uint8_t i = 0; i < node->num_channels; i++) {
		if (channels[i]->controller->id == node->id) {
			// This is a controller channel, initialize controller FSM context for lane 1
			ctrl_l1_ctx[ctrl_channel_key].curr_state = CTRL_L1_IDLE;
			ctrl_l1_ctx[ctrl_channel_key].channel_id = channels[i]->global_id;
			ctrl_l1_ctx[ctrl_channel_key].channel_key = i;
			ctrl_l1_ctx[ctrl_channel_key].last_rc = RC_OK;
			ctrl_l1_ctx[ctrl_channel_key].out_rc = RC_OK;
			ctrl_l1_ctx[ctrl_channel_key].abort_event = false;
			ctrl_l1_ctx[ctrl_channel_key].done_event = false;
			ctrl_l1_ctx[ctrl_channel_key].tx_in_progress = false;

			ctrl_channel_key++;

			DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [Init_Ctrl_L1_FSM] Initialized Controller L1 FSM for Channel Key: %d (Channel ID: %d)\n",
							ctrl_l1_ctx[ctrl_channel_key - 1].channel_key, channels[i]->global_id);
		}
	}

	if (ctrl_channel_key != node->num_controller_endpoints) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [Init_Ctrl_L1_FSM] Mismatch in expected number of controller channels. Expected: %d, Found: %d\n", node->num_controller_endpoints,
						ctrl_channel_key);
		return RC_INVALID_ARG;
	} else {
		DEBUG_LOG_INFO(APPLICATION_TAG "[CTRL] [Init_Ctrl_L1_FSM] Successfully initialized Controller L1 FSM for all controller channels. Total Controller Channels: %d\n",
					   node->num_controller_endpoints);
		return RC_OK;
	}
}

static inline void Ctrl_L1_Idle_State(controller_l1_context_t *ctx, void *msg, bool start) {

	if (start) {

		if (msg == NULL) {
			ctx->curr_state = CTRL_L1_FAILED;
			ctx->last_rc = RC_INVALID_ARG;

			DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [IDLE] Cannot start message transmission with NULL message pointer.\n", ctx->channel_id, APP_LANE1);
			return;
		}

		ctx->last_rc = Transport_Buffer_Message(ctx->channel_key, APP_LANE1, msg);
		if (ctx->last_rc != RC_OK) {

			ctx->curr_state = CTRL_L1_FAILED;

			DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [IDLE] Failed to buffer message. RC: %s\n", ctx->channel_id, APP_LANE1, RC_To_String(ctx->last_rc));

		} else {

			ctx->curr_state = CTRL_L1_SEND_MESSAGE;
			ctx->last_rc = RC_OK;
			ctx->out_rc = RC_BUSY;
			ctx->abort_event = false;
			ctx->done_event = false;
			ctx->tx_in_progress = false;

			DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [IDLE] Successfully buffered message\n", ctx->channel_id, APP_LANE1);
		}

	} else {

		ctx->curr_state = CTRL_L1_IDLE;
		ctx->out_rc = RC_OK;
	}
}

static inline void Ctrl_L1_Send_Message_State(controller_l1_context_t *ctx) {

	DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [SEND_MESSAGE] Attempting to send message\n", ctx->channel_id, APP_LANE1);

	if (!ctx->tx_in_progress) {

		ctx->last_rc = Transport_Send(ctx->channel_key, APP_LANE1);
		if (ctx->last_rc == RC_OK) {

			ctx->tx_in_progress = true;
			ctx->curr_state = CTRL_L1_SEND_MESSAGE;

			DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [SEND_MESSAGE] Message transmission started.\n", ctx->channel_id, APP_LANE1);

		} else {

			ctx->curr_state = CTRL_L1_FAILED;

			DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [SEND_MESSAGE] Failed to send message. RC: %s\n", ctx->channel_id, APP_LANE1, RC_To_String(ctx->last_rc));
		}

	} else {

		return_code_t done_event = Transport_Tx_Is_Done(ctx->channel_key, APP_LANE1);
		return_code_t abort_event = Transport_Tx_Is_Abort(ctx->channel_key, APP_LANE1);

		if (done_event == RC_OK_TRUE && abort_event == RC_OK_FALSE) {

			ctx->tx_in_progress = false;
			ctx->last_rc = RC_OK;
			ctx->curr_state = CTRL_L1_WAIT_FOR_ROUND;

			DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [SEND_MESSAGE] Message transmission completed.\n", ctx->channel_id, APP_LANE1);

		} else if (done_event == RC_OK_FALSE && abort_event == RC_OK_FALSE) {

			ctx->curr_state = CTRL_L1_SEND_MESSAGE; // Keep waiting for transmission to complete

			DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [SEND_MESSAGE] Message transmission in progress. Waiting for completion.\n", ctx->channel_id, APP_LANE1);

		} else {

			ctx->tx_in_progress = false;
			ctx->last_rc = RC_ERROR;
			ctx->curr_state = CTRL_L1_FAILED;

			DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [SEND_MESSAGE] Message transmission failed. RC: %s\n", ctx->channel_id, APP_LANE1, RC_To_String(ctx->last_rc));
		}
	}
}

static inline void Ctrl_L1_Wait_For_Round_State(controller_l1_context_t *ctx) {

	DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [WAIT_FOR_ROUND] Waiting for round to complete\n", ctx->channel_id, APP_LANE1);

	if (outstanding_req_cnt == 0) {

		ctx->curr_state = CTRL_L1_IDLE;
		ctx->last_rc = RC_OK;
		ctx->out_rc = RC_OK;
		ctx->done_event = true;

		DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [WAIT_FOR_ROUND] Round completed. Returning to IDLE state.\n", ctx->channel_id, APP_LANE1);

	} else if (outstanding_req_cnt > 0) {

		ctx->last_rc = RC_OK;
		ctx->curr_state = CTRL_L1_WAIT_FOR_ROUND; // Keep waiting for round to complete

		DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [WAIT_FOR_ROUND] Waiting for round to complete. Outstanding requests: %d\n", ctx->channel_id, APP_LANE1,
						outstanding_req_cnt);

	} else {

		ctx->curr_state = CTRL_L1_FAILED;
		ctx->last_rc = RC_ERROR;

		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [WAIT_FOR_ROUND] Invalid outstanding request count: %d\n", ctx->channel_id, APP_LANE1, outstanding_req_cnt);
	}
}

static inline void Ctrl_L1_Failed_State(controller_l1_context_t *ctx) {

	DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [FAILED] Operation failed.\n", ctx->channel_id, APP_LANE1);

	ctx->out_rc = RC_FATAL;
	ctx->abort_event = true; // Set abort_event flag to indicate failure

	if (outstanding_req_cnt > 0) {

		ctx->curr_state = CTRL_L1_WAIT_FOR_ROUND; // Wait for round to complete to ensure system stability before returning to IDLE

		DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [FAILED] Can't return to IDLE. Outstanding requests: %d. Moving to WAIT_FOR_ROUND state.\n", ctx->channel_id,
						APP_LANE1, outstanding_req_cnt);

	} else if (outstanding_req_cnt == 0) {

		ctx->curr_state = CTRL_L1_IDLE; // No outstanding requests, can return to IDLE immediately

		DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [FAILED] No outstanding requests. Returning to IDLE.\n", ctx->channel_id, APP_LANE1);

	} else {

		ctx->curr_state = CTRL_L1_FAILED;
		ctx->last_rc = RC_FATAL;

		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [FAILED] Invalid outstanding request count: %d\n", ctx->channel_id, APP_LANE1, outstanding_req_cnt);
	}
}

return_code_t Controller_L1_FSM_Poll(uint8_t ctrl_channel_key, void *msg, bool start) {

	controller_l1_context_t *ctx = &ctrl_l1_ctx[ctrl_channel_key];

	switch (ctx->curr_state) {
	case CTRL_L1_IDLE:
		Ctrl_L1_Idle_State(ctx, msg, start);
		break;
	case CTRL_L1_SEND_MESSAGE:
		Ctrl_L1_Send_Message_State(ctx);
		break;
	case CTRL_L1_WAIT_FOR_ROUND:
		Ctrl_L1_Wait_For_Round_State(ctx);
		break;
	case CTRL_L1_FAILED:
		Ctrl_L1_Failed_State(ctx);
		break;
	default:
		Ctrl_L1_Failed_State(ctx);
		break;
	}

	return RC_OK;
}

/*********************************************************************************************************************************
 * @par Controller API Implementation
 *********************************************************************************************************************************/

return_code_t Controller_Request(const halo_channel_t *channel) {

	if (Is_Channel_Valid(channel) != RC_OK_TRUE) {
		return RC_INTEGRITY_FAIL; // Only controller node can send on controller lane
	}

	if (Is_Node_Controller_For_Channel(channel) != RC_OK_TRUE) {
		return RC_PERM_DENIED; // Only controller node can send on controller lane
	}

	int16_t channel_key = Get_Channel_Key_From_ID(channel->global_id);
	if (channel_key < 0) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Request] Invalid channel key for channel ID: %d\n", channel->global_id, APP_LANE0, channel->global_id);
		return RC_INTEGRITY_FAIL; // Invalid channel
	}

	int16_t ctrl_channel_key = Get_Controller_Channel_Key_From_Channel_Key(channel_key);
	if (ctrl_channel_key < 0) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Request] Invalid controller channel key for channel ID: %d\n", channel->global_id, APP_LANE0,
						channel->global_id);
		return RC_INTEGRITY_FAIL; // Not a valid controller channel
	}

	controller_l0_context_t *ctx = &ctrl_l0_ctx[ctrl_channel_key];

	if (ctx->out_rc == RC_BUSY) {
		DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Request] Previous request still in progress. Cannot send new request yet.\n", channel->global_id,
						APP_LANE0);
		return RC_BUSY; // Previous request still being processed
	}

	if (!ctx->resp_ack_event) {
		DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Request] Previous response not yet acknowledged. Cannot send new request yet.\n", channel->global_id,
						APP_LANE0);
		return RC_BUSY; // Previous response not yet acknowledged by controller
	}

	DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] Controller_Request called\n", channel->global_id, APP_LANE0);

	(void)Controller_L0_FSM_Poll(ctrl_channel_key, true);

	return RC_OK;
}

return_code_t Controller_Receive(const halo_channel_t *channel, void *msg) {

	if (Is_Channel_Valid(channel) != RC_OK_TRUE) {
		return RC_INTEGRITY_FAIL; // Only controller node can receive on controller lane
	}

	if (Is_Node_Controller_For_Channel(channel) != RC_OK_TRUE) {
		return RC_PERM_DENIED; // Only controller node can receive on controller lane
	}

	if (Does_Msg_Match_Channel_And_Lane(channel, APP_LANE0, msg) != RC_OK_TRUE) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Receive] Message does not match expected descriptor for this channel and lane.\n", channel->global_id,
						APP_LANE0);
		return RC_INTEGRITY_FAIL; // Message does not match expected descriptor for this channel and lane
	}

	int16_t channel_key = Get_Channel_Key_From_ID(channel->global_id);
	if (channel_key < 0) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Receive] Invalid channel key for channel ID: %d\n", channel->global_id, APP_LANE0, channel->global_id);
		return RC_INTEGRITY_FAIL; // Invalid channel
	}

	int16_t ctrl_channel_key = Get_Controller_Channel_Key_From_Channel_Key(Get_Channel_Key_From_ID(channel->global_id));
	if (ctrl_channel_key < 0) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Receive] Invalid controller channel key for channel ID: %d\n", channel->global_id, APP_LANE0,
						channel->global_id);
		return RC_INTEGRITY_FAIL; // Not a valid controller channel
	}

	controller_l0_context_t *ctx = &ctrl_l0_ctx[ctrl_channel_key];

	if (ctx->timeout_event) {

		ctx->resp_ack_event = true; // Acknowledge the timed-out response so that controller can move on to next request
		ctx->timeout_event = false; // Clear timeout event flag for next round

		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Receive] Response timed out.\n", channel->global_id, APP_LANE0);

		return RC_TIMEOUT; // Previous request timed out, so response is not valid
	}

	if (ctx->abort_event) {

		ctx->resp_ack_event = true; // Acknowledge the failed response so that controller can move on to next request
		ctx->abort_event = false;	// Clear abort event flag for next round

		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Receive] Previous operation failed. Cannot receive response.\n", channel->global_id, APP_LANE0);

		return RC_FATAL; // Previous operation failed, so response is not valid
	}

	if (!ctx->resp_available_event) {

		DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Receive] No response available to receive\n", channel->global_id, APP_LANE0);

		return RC_OK_NO_DATA; // No response available yet
	}

	return_code_t rc = Transport_Receive(channel_key, APP_LANE0, msg);
	if (rc != RC_OK) {

		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Receive] Failed to receive response message. RC: %s\n", channel->global_id, APP_LANE0, RC_To_String(rc));

		return RC_FATAL; // Error while receiving
	}

	ctx->resp_available_event = false; // Clear response available flag after receiving
	ctx->resp_ack_event = true;		   // Acknowledge the received response so that controller can move on to next request

	DEBUG_LOG_INFO(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Receive] Received response message successfully\n", channel->global_id, APP_LANE0);

	return RC_OK;
}

return_code_t Controller_Send(void *msg, const halo_channel_t *channel) {

	DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Send] Function called\n", channel->global_id, APP_LANE1);

	if (Is_Channel_Valid(channel) != RC_OK_TRUE) {

		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Send] Called with invalid channel pointer.\n", channel->global_id, APP_LANE1);

		return RC_INTEGRITY_FAIL; // Only controller node can send on controller lane
	}

	if (Is_Node_Controller_For_Channel(channel) != RC_OK_TRUE) {

		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Send] Called on channel where node %d is not the controller.\n", channel->global_id, APP_LANE1, node->id);

		return RC_PERM_DENIED; // Only controller node can send on controller lane
	}

	if (Does_Msg_Match_Channel_And_Lane(channel, APP_LANE1, msg) != RC_OK_TRUE) {

		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Send] Message does not match expected descriptor for this channel and lane.\n", channel->global_id,
						APP_LANE1);

		return RC_INTEGRITY_FAIL; // Message does not match expected descriptor for this channel and lane
	}

	int16_t channel_key = Get_Channel_Key_From_ID(channel->global_id);
	if (channel_key < 0 || channel_key >= node->num_channels) {

		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Send] Invalid channel key for channel ID: %d\n", channel->global_id, APP_LANE1, channel->global_id);

		return RC_INVALID_ARG;
	}

	int16_t ctrl_channel_key = Get_Controller_Channel_Key_From_Channel_Key(channel_key);
	if (ctrl_channel_key < 0) {

		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Send] Invalid controller channel key for channel ID: %d\n", channel->global_id, APP_LANE1,
						channel->global_id);

		return RC_INTEGRITY_FAIL; // Not a valid controller channel
	}

	controller_l1_context_t *ctx = &ctrl_l1_ctx[ctrl_channel_key];

	if (ctx->out_rc == RC_BUSY) {

		DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Send] Previous message still being processed. Cannot send new message yet.\n", channel->global_id,
						APP_LANE1);

		return RC_BUSY; // Previous message still being processed
	}

	(void)Controller_L1_FSM_Poll(ctrl_channel_key, msg, true);

	return RC_OK;
}

return_code_t Controller_Send_Is_Done(const halo_channel_t *channel) {

	DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Send_Is_Done] Function called\n", channel->global_id, APP_LANE1);

	if (Is_Channel_Valid(channel) != RC_OK_TRUE) {

		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Send_Is_Done] Called with invalid channel pointer.\n", channel->global_id, APP_LANE1);

		return RC_INTEGRITY_FAIL; // Only controller node can send on controller lane
	}

	if (Is_Node_Controller_For_Channel(channel) != RC_OK_TRUE) {

		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Send_Is_Done] Called on channel where node %d is not the controller.\n", channel->global_id, APP_LANE1,
						node->id);

		return RC_PERM_DENIED; // Only controller node can send on controller lane
	}

	int16_t channel_key = Get_Channel_Key_From_ID(channel->global_id);
	if (channel_key < 0 || channel_key >= node->num_channels) {

		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Send_Is_Done] Invalid channel key for channel ID: %d\n", channel->global_id, APP_LANE1,
						channel->global_id);

		return RC_INVALID_ARG;
	}

	int16_t ctrl_channel_key = Get_Controller_Channel_Key_From_Channel_Key(channel_key);
	if (ctrl_channel_key < 0) {

		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Send_Is_Done] Invalid controller channel key for channel ID: %d\n", channel->global_id, APP_LANE1,
						channel->global_id);

		return RC_INTEGRITY_FAIL; // Not a valid controller channel
	}

	controller_l1_context_t *ctx = &ctrl_l1_ctx[ctrl_channel_key];

	if (ctx->done_event) {

		ctx->done_event = false; // Clear done event flag for next round

		DEBUG_LOG_INFO(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Send_Is_Done] Message processing completed.\n", channel->global_id, APP_LANE1);

		return RC_OK_TRUE;

	} else {

		DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Send_Is_Done] Message still being processed.\n", channel->global_id, APP_LANE1);

		return RC_OK_FALSE;
	}
}

return_code_t Controller_Send_Is_Abort(const halo_channel_t *channel) {

	DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Send_Is_Abort] Function called\n", channel->global_id, APP_LANE1);

	if (Is_Channel_Valid(channel) != RC_OK_TRUE) {

		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Send_Is_Abort] Called with invalid channel pointer.\n", channel->global_id, APP_LANE1);

		return RC_INTEGRITY_FAIL; // Only controller node can send on controller lane
	}

	if (Is_Node_Controller_For_Channel(channel) != RC_OK_TRUE) {

		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Send_Is_Abort] Called on channel where node %d is not the controller.\n", channel->global_id, APP_LANE1,
						node->id);

		return RC_PERM_DENIED; // Only controller node can send on controller lane
	}

	int16_t channel_key = Get_Channel_Key_From_ID(channel->global_id);
	if (channel_key < 0 || channel_key >= node->num_channels) {

		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Send_Is_Abort] Invalid channel key for channel ID: %d\n", channel->global_id, APP_LANE1,
						channel->global_id);

		return RC_INVALID_ARG;
	}

	int16_t ctrl_channel_key = Get_Controller_Channel_Key_From_Channel_Key(channel_key);
	if (ctrl_channel_key < 0) {

		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Send_Is_Abort] Invalid controller channel key for channel ID: %d\n", channel->global_id, APP_LANE1,
						channel->global_id);

		return RC_INTEGRITY_FAIL; // Not a valid controller channel
	}

	controller_l1_context_t *ctx = &ctrl_l1_ctx[ctrl_channel_key];

	if (ctx->abort_event) {

		ctx->abort_event = false; // Clear abort event flag for next round

		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Send_Is_Abort] Previous operation failed.\n", channel->global_id, APP_LANE1);

		return RC_OK_TRUE;

	} else {

		DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Send_Is_Abort] No failure detected for current message.\n", channel->global_id, APP_LANE1);

		return RC_OK_FALSE;
	}
}

return_code_t Controller_Send_Is_Busy(const halo_channel_t *channel) {

	DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Send_Is_Busy] Function called\n", channel->global_id, APP_LANE1);

	if (Is_Channel_Valid(channel) != RC_OK_TRUE) {

		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Send_Is_Busy] Called with invalid channel pointer.\n", channel->global_id, APP_LANE1);

		return RC_INTEGRITY_FAIL; // Only controller node can send on controller lane
	}

	if (Is_Node_Controller_For_Channel(channel) != RC_OK_TRUE) {

		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Send_Is_Busy] Called on channel where node %d is not the controller.\n", channel->global_id, APP_LANE1,
						node->id);

		return RC_PERM_DENIED; // Only controller node can send on controller lane
	}

	int16_t channel_key = Get_Channel_Key_From_ID(channel->global_id);
	if (channel_key < 0 || channel_key >= node->num_channels) {

		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Send_Is_Busy] Invalid channel key for channel ID: %d\n", channel->global_id, APP_LANE1,
						channel->global_id);

		return RC_INVALID_ARG;
	}

	int16_t ctrl_channel_key = Get_Controller_Channel_Key_From_Channel_Key(channel_key);
	if (ctrl_channel_key < 0) {

		DEBUG_LOG_ERROR(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Send_Is_Busy] Invalid controller channel key for channel ID: %d\n", channel->global_id, APP_LANE1,
						channel->global_id);

		return RC_INTEGRITY_FAIL; // Not a valid controller channel
	}

	controller_l1_context_t *ctx = &ctrl_l1_ctx[ctrl_channel_key];

	if (ctx->out_rc == RC_BUSY) {

		DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Send_Is_Busy] Previous message still being processed. Controller is busy.\n", channel->global_id,
						APP_LANE1);
		return RC_OK_TRUE;
	} else {
		DEBUG_LOG_TRACE(APPLICATION_TAG "[CTRL] [CH %d][L %d] [Controller_Send_Is_Busy] No message currently being processed. Controller is not busy.\n", channel->global_id,
						APP_LANE1);
		return RC_OK_FALSE;
	}
}

/*********************************************************************************************************************************
 * @par Responder Endpoint Implementation
 *
 * @details
 *
 *
 *
 *
 *
 *********************************************************************************************************************************/

/*********************************************************************************************************************************
 * @par Responder L0 FSM Types
 *********************************************************************************************************************************/

typedef enum { RESP_L0_IDLE, RESP_L0_WAIT_FOR_REQUEST, RESP_L0_PROCESS_REQUEST, RESP_L0_SEND_RESPONSE, RESP_L0_FAILED } responder_l0_state_t;

typedef struct {
	responder_l0_state_t curr_state;
	uint16_t channel_id;
	uint8_t channel_key;
	return_code_t last_rc;
	return_code_t out_rc;
	bool abort_event;
	bool done_event; // Indicates whether the responder has acknowledged the request and sent response
	bool tx_in_progress;
	uint8_t request_tag;
	uint8_t *staging; // Application staging buffer: Respond writes the latest value here (never blocks);
					  // the FSM copies it into the transport buffer on its normal IDLE/WAIT polls.
} responder_l0_context_t;

/*********************************************************************************************************************************
 * @par Responder L0 FSM Data Structures and Initialization
 *********************************************************************************************************************************/

static responder_l0_context_t *resp_l0_ctx = NULL;

static inline return_code_t Init_Resp_L0_FSM(void) {

	resp_l0_ctx = (responder_l0_context_t *)malloc(sizeof(responder_l0_context_t) * node->num_responder_endpoints);
	if (resp_l0_ctx == NULL) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "Failed to allocate memory for Responder L0 FSM context.\n");
		return RC_FATAL;
	}

	const halo_channel_t *const *channels = node->channels;
	uint8_t node_id = node->id;
	uint8_t resp_channel_key = 0;

	for (uint8_t i = 0; i < node->num_channels; i++) {
		if (channels[i]->responder->id == node_id) {
			// This is a responder channel, initialize responder FSM context for lane 0
			resp_l0_ctx[resp_channel_key].curr_state = RESP_L0_IDLE;
			resp_l0_ctx[resp_channel_key].channel_id = channels[i]->global_id;
			resp_l0_ctx[resp_channel_key].channel_key = i;
			resp_l0_ctx[resp_channel_key].last_rc = RC_OK;
			resp_l0_ctx[resp_channel_key].out_rc = RC_OK;
			resp_l0_ctx[resp_channel_key].tx_in_progress = false;
			resp_l0_ctx[resp_channel_key].abort_event = false;
			resp_l0_ctx[resp_channel_key].done_event = false;
			resp_l0_ctx[resp_channel_key].request_tag = 0;
			resp_l0_ctx[resp_channel_key].staging = (uint8_t *)malloc(channels[i]->lane0_message->object_size);
			if (resp_l0_ctx[resp_channel_key].staging == NULL) {
				DEBUG_LOG_ERROR(APPLICATION_TAG "Failed to allocate Responder L0 staging buffer.\n");
				return RC_FATAL;
			}

			resp_channel_key++;

			DEBUG_LOG_INFO(APPLICATION_TAG "Initialized Responder L0 FSM for Channel Key: %d (Channel ID: %d)\n", resp_l0_ctx[resp_channel_key - 1].channel_key,
						   channels[i]->global_id);
		}
	}

	if (resp_channel_key != node->num_responder_endpoints) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "Mismatch in expected number of responder channels. Expected: %d, Found: %d\n", node->num_responder_endpoints, resp_channel_key);
		return RC_INVALID_ARG;
	} else {
		DEBUG_LOG_INFO(APPLICATION_TAG "Successfully initialized Responder L0 FSM for all responder channels. Total Responder Channels: %d\n", node->num_responder_endpoints);
		return RC_OK;
	}
}

/*********************************************************************************************************************************
 * @par Responder L0 FSM Implementation
 *********************************************************************************************************************************/

static inline void Resp_L0_Idle_State(responder_l0_context_t *ctx, void *msg, bool buffer_resp) {

	if (msg == NULL && buffer_resp == APP_RESP_L0_BUFFER_RESPONSE) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [IDLE] Received NULL message pointer for channel %d, lane %d\n", ctx->channel_id, APP_LANE0);
		ctx->last_rc = RC_INVALID_ARG;
		ctx->curr_state = RESP_L0_FAILED;
		return;
	}

	if (!buffer_resp) {
		ctx->curr_state = RESP_L0_IDLE;
	} else {
		ctx->last_rc = RC_OK;
		ctx->out_rc = RC_OK;
		ctx->tx_in_progress = false;
		ctx->abort_event = false;
		ctx->done_event = false;

		DEBUG_LOG_TRACE(APPLICATION_TAG "[RESP] [CH %d][L %d] [IDLE] Preparing response message.\n", ctx->channel_id, APP_LANE0);

		ctx->last_rc = Transport_Buffer_Message(ctx->channel_key, APP_LANE0, ctx->staging);
		if (ctx->last_rc == RC_OK) {
			DEBUG_LOG_TRACE(APPLICATION_TAG "[RESP] [CH %d][L %d] [IDLE] Response message buffered successfully.\n", ctx->channel_id, APP_LANE0);
			ctx->curr_state = RESP_L0_WAIT_FOR_REQUEST;
		} else {
			DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [IDLE] Failed to buffer response message. RC: %s\n", ctx->channel_id, APP_LANE0, RC_To_String(ctx->last_rc));
			ctx->curr_state = RESP_L0_FAILED;
			return;
		}
	}
}

static inline void Resp_L0_Wait_For_Request_State(responder_l0_context_t *ctx, void *msg, bool buffer_resp) {

	DEBUG_LOG_TRACE(APPLICATION_TAG "[RESP] [CH %d][L %d] [WAIT_FOR_REQUEST] Waiting for request\n", ctx->channel_id, APP_LANE0);

	if (msg == NULL && buffer_resp == APP_RESP_L0_BUFFER_RESPONSE) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [WAIT_FOR_REQUEST] Received NULL message pointer for channel %d, lane %d\n", ctx->channel_id, APP_LANE0);
		ctx->last_rc = RC_INVALID_ARG;
		ctx->curr_state = RESP_L0_FAILED;
		return;
	}

	// Fresher arrived while waiting, prepare it as response
	if (buffer_resp) {
		DEBUG_LOG_TRACE(APPLICATION_TAG "[RESP] [CH %d][L %d] [WAIT_FOR_REQUEST] Preparing response message\n", ctx->channel_id, APP_LANE0);
		ctx->last_rc = Transport_Buffer_Message(ctx->channel_key, APP_LANE0, ctx->staging);
		if (ctx->last_rc == RC_OK) {
			DEBUG_LOG_TRACE(APPLICATION_TAG "[RESP] [CH %d][L %d] [WAIT_FOR_REQUEST] Response message buffered successfully\n", ctx->channel_id, APP_LANE0);
			ctx->curr_state = RESP_L0_WAIT_FOR_REQUEST;
		} else {
			DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [WAIT_FOR_REQUEST] Failed to buffer response message. RC: %s\n", ctx->channel_id, APP_LANE0,
							RC_To_String(ctx->last_rc));
			ctx->curr_state = RESP_L0_FAILED;
			return;
		}
	}

	ctx->last_rc = Transport_Rx_Is_Msg_Available(ctx->channel_key, APP_LANE0);
	if (ctx->last_rc == RC_OK_TRUE) {
		DEBUG_LOG_TRACE(APPLICATION_TAG "[RESP] [CH %d][L %d] [WAIT_FOR_REQUEST] Request available\n", ctx->channel_id, APP_LANE0);
		ctx->curr_state = RESP_L0_PROCESS_REQUEST;
		ctx->out_rc = RC_BUSY;
	} else {
		ctx->curr_state = RESP_L0_WAIT_FOR_REQUEST;
	}
}

static inline void Resp_L0_Process_Request_State(responder_l0_context_t *ctx) {

	DEBUG_LOG_TRACE(APPLICATION_TAG "[RESP] [CH %d][L %d] [PROCESS REQUEST] Processing request\n", ctx->channel_id, APP_LANE0);
	halo_request_message_t request_msg = HALO_REQUEST_MESSAGE_INIT;
	ctx->last_rc = Transport_Receive(ctx->channel_key, APP_LANE0, &request_msg);
	if (ctx->last_rc == RC_OK) {
		ctx->last_rc = Transport_Rx_Get_Tag(ctx->channel_key, APP_LANE0, &ctx->request_tag);
		if (ctx->last_rc != RC_OK) {
			DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [PROCESS REQUEST] Failed to retrieve request tag. RC: %s\n",
							ctx->channel_id, APP_LANE0, RC_To_String(ctx->last_rc));
			ctx->curr_state = RESP_L0_FAILED;
			return;
		}

		ctx->last_rc = Transport_Tx_Set_Tag(ctx->channel_key, APP_LANE0, ctx->request_tag);
		if (ctx->last_rc != RC_OK) {
			DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [PROCESS REQUEST] Failed to associate response with request tag %u. RC: %s\n",
							ctx->channel_id, APP_LANE0, ctx->request_tag, RC_To_String(ctx->last_rc));
			ctx->curr_state = RESP_L0_FAILED;
			return;
		}

		DEBUG_LOG_TRACE(APPLICATION_TAG "[RESP] [CH %d][L %d] [PROCESS REQUEST] Received request message: %llu\n", ctx->channel_id, APP_LANE0, TEST_MSG_DESCRIPTOR.message_size,
						request_msg.timestamp);
		ctx->curr_state = RESP_L0_SEND_RESPONSE;
	} else {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [PROCESS REQUEST] Failed to receive request message. RC: %s\n", ctx->channel_id, APP_LANE0,
						RC_To_String(ctx->last_rc));
		ctx->curr_state = RESP_L0_FAILED;
	}
}

static inline void Resp_L0_Send_Response_State(responder_l0_context_t *ctx) {

	DEBUG_LOG_TRACE(APPLICATION_TAG "[RESP] [CH %d][L %d] [SEND RESPONSE] Sending response\n", ctx->channel_id, APP_LANE0);

	if (!ctx->tx_in_progress) {

		ctx->last_rc = Transport_Send(ctx->channel_key, APP_LANE0);
		if (ctx->last_rc == RC_OK) {
			DEBUG_LOG_TRACE(APPLICATION_TAG "[RESP] [CH %d][L %d] [SEND RESPONSE] Started response transmission\n", ctx->channel_id, APP_LANE0);
			ctx->tx_in_progress = true;
		} else {
			DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [SEND RESPONSE] Failed to start response transmission. RC: %s\n", ctx->channel_id, APP_LANE0,
							RC_To_String(ctx->last_rc));
			ctx->curr_state = RESP_L0_FAILED;
		}
	} else {

		return_code_t done_event = Transport_Tx_Is_Done(ctx->channel_key, APP_LANE0);
		return_code_t abort_event = Transport_Tx_Is_Abort(ctx->channel_key, APP_LANE0);

		if (done_event == RC_OK_TRUE && abort_event == RC_OK_FALSE) {
			DEBUG_LOG_INFO(APPLICATION_TAG "[RESP] [CH %d][L %d] [SEND RESPONSE] Response transmission completed\n", ctx->channel_id, APP_LANE0);
			ctx->tx_in_progress = false;
			ctx->done_event = true;			// Set done event to indicate successful transmission
			ctx->curr_state = RESP_L0_IDLE; // Go back to idle after successful send
			ctx->out_rc = RC_OK;
		} else if (done_event == RC_OK_FALSE && abort_event == RC_OK_FALSE) {
			DEBUG_LOG_TRACE(APPLICATION_TAG "[RESP] [CH %d][L %d] [SEND RESPONSE] Response transmission still in progress\n", ctx->channel_id, APP_LANE0);
			ctx->curr_state = RESP_L0_SEND_RESPONSE; // Still sending
		} else {
			DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [SEND RESPONSE] Error during response transmission. RC: %s\n", ctx->channel_id, APP_LANE0,
							RC_To_String(ctx->last_rc));
			ctx->tx_in_progress = false;
			ctx->curr_state = RESP_L0_FAILED; // Go to failed state on error
		}
	}
}

static inline void Resp_L0_Failed_State(responder_l0_context_t *ctx) {

	DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [FAILED] Entered failed state\n", ctx->channel_id, APP_LANE0);
	ctx->abort_event = true;
	ctx->curr_state = RESP_L0_IDLE; // For now, just reset to idle
	ctx->out_rc = RC_FATAL;
}

static inline return_code_t Responder_L0_FSM_Poll(uint16_t resp_channel_key, uint8_t lane_id, void *msg, bool start) {

	responder_l0_context_t *ctx = &resp_l0_ctx[resp_channel_key];

	switch (ctx->curr_state) {
	case RESP_L0_IDLE:
		Resp_L0_Idle_State(ctx, msg, start);
		break;
	case RESP_L0_WAIT_FOR_REQUEST:
		Resp_L0_Wait_For_Request_State(ctx, msg, start);
		break;
	case RESP_L0_PROCESS_REQUEST:
		Resp_L0_Process_Request_State(ctx);
		break;
	case RESP_L0_SEND_RESPONSE:
		Resp_L0_Send_Response_State(ctx);
		break;
	case RESP_L0_FAILED:
		Resp_L0_Failed_State(ctx);
		break;
	default:
		Resp_L0_Failed_State(ctx);
		break;
	}

	DEBUG_LOG_TRACE(APPLICATION_TAG "[RESP] [CH %d][L %d] Polled FSM. Current State: %d, Last RC: %s, Out RC: %s\n", ctx->channel_id, lane_id, ctx->curr_state,
					RC_To_String(ctx->last_rc), RC_To_String(ctx->out_rc));

	return RC_OK;
}

/*********************************************************************************************************************************
 * @par RESPONDER L1 FSM IMPLEMENTATION
 *********************************************************************************************************************************/
typedef enum { RESP_L1_IDLE, RESP_L1_WAIT_FOR_MSG, RESP_L1_FAILED } responder_l1_state_t;

typedef struct {
	responder_l1_state_t curr_state;
	uint16_t channel_id;
	uint8_t channel_key;
	return_code_t last_rc;
	return_code_t out_rc;
	bool msg_available_event; // Indicates whether a new message has arrived on lane 1 that requires processing
	bool abort_event;		  // Indicates whether the responder has detected an error condition while processing the message
} responder_l1_context_t;

static responder_l1_context_t *resp_l1_ctx = NULL;

static inline return_code_t Init_Resp_L1_FSM(void) {

	resp_l1_ctx = (responder_l1_context_t *)malloc(sizeof(responder_l1_context_t) * node->num_responder_endpoints);
	if (resp_l1_ctx == NULL) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [Init Responder L1 FSM] Failed to allocate memory for Responder L1 FSM context.\n");
		return RC_FATAL;
	}

	const halo_channel_t *const *channels = node->channels;
	uint8_t node_id = node->id;
	uint8_t resp_channel_key = 0;

	for (uint8_t i = 0; i < node->num_channels; i++) {
		if (channels[i]->responder->id == node_id) {
			// This is a responder channel, initialize responder FSM context for lane 1
			resp_l1_ctx[resp_channel_key].curr_state = RESP_L1_IDLE;
			resp_l1_ctx[resp_channel_key].channel_id = channels[i]->global_id;
			resp_l1_ctx[resp_channel_key].channel_key = i;
			resp_l1_ctx[resp_channel_key].last_rc = RC_OK;
			resp_l1_ctx[resp_channel_key].out_rc = RC_OK;
			resp_l1_ctx[resp_channel_key].msg_available_event = false;
			resp_l1_ctx[resp_channel_key].abort_event = false;

			resp_channel_key++;

			DEBUG_LOG_INFO(APPLICATION_TAG "[RESP] [Init Responder L1 FSM] Initialized Responder L1 FSM for Channel Key: %d (Channel ID: %d)\n",
						   resp_l1_ctx[resp_channel_key - 1].channel_key, channels[i]->global_id);
		}
	}

	if (resp_channel_key != node->num_responder_endpoints) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [Init Responder L1 FSM] Mismatch in expected number of responder channels. Expected: %d, Found: %d\n",
						node->num_responder_endpoints, resp_channel_key);
		return RC_INVALID_ARG;
	} else {
		DEBUG_LOG_INFO(APPLICATION_TAG "[RESP] [Init Responder L1 FSM] Successfully initialized Responder L1 FSM for all responder channels. Total Responder Channels: %d\n",
					   node->num_responder_endpoints);
		return RC_OK;
	}
}

static inline void Resp_L1_Idle_State(responder_l1_context_t *ctx) {
	// For this example, the responder does not expect to receive messages on lane 1, so we will just stay idle
	DEBUG_LOG_TRACE(APPLICATION_TAG "[RESP] [CH %d][L %d] [IDLE] Currently in idle state.\n", ctx->channel_id, APP_LANE1);

	ctx->curr_state = RESP_L1_WAIT_FOR_MSG; // Transition to wait for message state to simulate waiting for messages on lane 1
	ctx->last_rc = RC_OK;
	ctx->out_rc = RC_OK;
}

static inline void Resp_L1_Wait_For_Msg_State(responder_l1_context_t *ctx) {
	// For this example, the responder does not expect to receive messages on lane 1, so we will just stay idle
	DEBUG_LOG_TRACE(APPLICATION_TAG "[RESP] [CH %d][L %d] [WAIT_FOR_MSG] Currently waiting for messages, but no operations defined for lane 1 in this example.\n", ctx->channel_id,
					APP_LANE1);

	if (Transport_Rx_Is_Msg_Available(ctx->channel_key, APP_LANE1) == RC_OK_TRUE) {
		ctx->msg_available_event = true;
		DEBUG_LOG_TRACE(APPLICATION_TAG "[RESP] [CH %d][L %d] [WAIT_FOR_MSG] Message available to receive.\n", ctx->channel_id, APP_LANE1);
	} else if (Transport_Rx_Is_Msg_Available(ctx->channel_key, APP_LANE1) == RC_OK_FALSE) {
		ctx->curr_state = RESP_L1_WAIT_FOR_MSG; // Stay in wait for message state
	} else {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [WAIT_FOR_MSG] Error checking for message availability. RC: %s\n", ctx->channel_id, APP_LANE1,
						RC_To_String(ctx->last_rc));
		ctx->curr_state = RESP_L1_FAILED; // Go to failed state on error
	}
}

static inline void Resp_L1_Failed_State(responder_l1_context_t *ctx) {
	DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [FAILED] Entered failed state\n", ctx->channel_id, APP_LANE1);
	ctx->curr_state = RESP_L1_IDLE; // For now, just reset to idle
	ctx->out_rc = RC_FATAL;
	ctx->abort_event = true;
}

static inline void Responder_L1_FSM_Poll(int16_t resp_channel_key, uint8_t lane_id) {

	responder_l1_context_t *ctx = &resp_l1_ctx[resp_channel_key];

	switch (ctx->curr_state) {
	case RESP_L1_IDLE:
		Resp_L1_Idle_State(ctx);
		break;
	case RESP_L1_WAIT_FOR_MSG:
		Resp_L1_Wait_For_Msg_State(ctx);
		break;
	case RESP_L1_FAILED:
		Resp_L1_Failed_State(ctx);
		break;
	default:
		Resp_L1_Failed_State(ctx);
		break;
	}

	DEBUG_LOG_TRACE(APPLICATION_TAG "[RESP] [CH %d][L %d] Polled L1 FSM. Current State: %d, Last RC: %s, Out RC: %s\n", ctx->channel_id, lane_id, ctx->curr_state,
					RC_To_String(ctx->last_rc), RC_To_String(ctx->out_rc));
}

/*********************************************************************************************************************************
 * @par Responder API Implementation
 *********************************************************************************************************************************/

return_code_t Responder_Respond(const halo_channel_t *channel, void *msg) {

	if (Is_Channel_Valid(channel) != RC_OK_TRUE) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Respond] Called with invalid channel pointer.\n", channel->global_id, APP_LANE0);
		return RC_INVALID_ARG; // Invalid channel pointer
	}

	if (Is_Node_Responder_For_Channel(channel) != RC_OK_TRUE) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Respond] Called on channel where node %d is not the responder.\n", channel->global_id, APP_LANE0,
						node->id);
		return RC_PERM_DENIED; // Only responder node can send on responder lane
	}

	

	if (Does_Msg_Match_Channel_And_Lane(channel, APP_LANE0, msg) != RC_OK_TRUE) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Respond] Called with message that does not match expected descriptor.\n", channel->global_id, APP_LANE0);
		return RC_INTEGRITY_FAIL; // Message does not match expected descriptor for this channel and lane
	}

	int16_t channel_key = Get_Channel_Key_From_ID(channel->global_id);
	if (channel_key < 0) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Respond] Invalid channel key for channel ID: %d\n", channel->global_id, APP_LANE0, channel->global_id);
		return RC_INTEGRITY_FAIL; // Invalid channel
	}

	int16_t resp_channel_key = Get_Responder_Channel_Key_From_Channel_Key(channel_key);
	if (resp_channel_key < 0) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Respond] Invalid responder channel key for channel ID: %d\n", channel->global_id, APP_LANE0,
						channel->global_id);
		return RC_INTEGRITY_FAIL; // Not a valid responder channel
	}

	responder_l0_context_t *ctx = &resp_l0_ctx[resp_channel_key];

	// Stage the latest value into the application buffer first. This always succeeds and never
	// blocks, so the responder can prepare a new message even while a previous response is in
	// flight. The FSM copies this buffer into the transport buffer on its normal IDLE/WAIT polls.
	memcpy(ctx->staging, msg, channel->lane0_message->object_size);

	if (ctx->out_rc == RC_BUSY) {
		// A response is still being sent: the value is staged and will be picked up by the FSM
		// once it returns to IDLE/WAIT. The in-flight response is unaffected.
		DEBUG_LOG_TRACE(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Respond] Response in flight; latest value staged.\n", channel->global_id, APP_LANE0);
		return RC_OK;
	}

	DEBUG_LOG_INFO(APPLICATION_TAG "[RESP] [CH %d][L %d] Responder_Respond called\n", channel->global_id, APP_LANE0);

	return_code_t rc = Responder_L0_FSM_Poll(resp_channel_key, APP_LANE0, msg, APP_RESP_L0_BUFFER_RESPONSE);
	if (rc == RC_OK) {
		DEBUG_LOG_TRACE(APPLICATION_TAG "[RESP] [CH %d][L %d] Responder_Respond polled L0 FSM successfully\n", channel->global_id, APP_LANE0);
		return RC_OK;
	} else {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] Responder_Respond failed to poll Responder L0 FSM. RC: %s\n", channel->global_id, APP_LANE0, RC_To_String(rc));
		return RC_FATAL;
	}
}

return_code_t Responder_Respond_Is_Done(const halo_channel_t *channel) {

	if (Is_Channel_Valid(channel) != RC_OK_TRUE) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Respond_Is_Done] Called with invalid channel pointer.\n", channel->global_id, APP_LANE0);
		return RC_INVALID_ARG; // Invalid channel pointer
	}

	if (Is_Node_Responder_For_Channel(channel) != RC_OK_TRUE) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Respond_Is_Done] Called on channel where node %d is not the responder.\n", channel->global_id, APP_LANE0,
						node->id);
		return RC_PERM_DENIED; // Only responder node can send on responder lane
	}

	int16_t channel_key = Get_Channel_Key_From_ID(channel->global_id);
	if (channel_key < 0) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Respond_Is_Done] Invalid channel key for channel ID: %d\n", channel->global_id, APP_LANE0,
						channel->global_id);
		return RC_INTEGRITY_FAIL; // Invalid channel
	}

	int16_t resp_channel_key = Get_Responder_Channel_Key_From_Channel_Key(channel_key);
	if (resp_channel_key < 0) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Respond_Is_Done] Invalid responder channel key for channel ID: %d\n", channel->global_id, APP_LANE0,
						channel->global_id);
		return RC_INTEGRITY_FAIL; // Not a valid responder channel
	}

	responder_l0_context_t *ctx = &resp_l0_ctx[resp_channel_key];

	if (ctx->done_event) {
		DEBUG_LOG_INFO(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Respond_Is_Done] Response transmission completed.\n", channel->global_id, APP_LANE0);
		ctx->done_event = false; // Clear done event flag for next round
		return RC_OK_TRUE;
	} else {
		DEBUG_LOG_TRACE(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Respond_Is_Done] Response still being transmitted.\n", channel->global_id, APP_LANE0);
		return RC_OK_FALSE;
	}
}

return_code_t Responder_Respond_Is_Abort(const halo_channel_t *channel) {

	if (Is_Channel_Valid(channel) != RC_OK_TRUE) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Respond_Is_Abort] Called with invalid channel pointer.\n", channel->global_id, APP_LANE0);
		return RC_INVALID_ARG; // Invalid channel pointer
	}

	if (Is_Node_Responder_For_Channel(channel) != RC_OK_TRUE) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Respond_Is_Abort] Called on channel where node %d is not the responder.\n", channel->global_id, APP_LANE0,
						node->id);
		return RC_PERM_DENIED; // Only responder node can send on responder lane
	}

	int16_t channel_key = Get_Channel_Key_From_ID(channel->global_id);
	if (channel_key < 0) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Respond_Is_Abort] Invalid channel key for channel ID: %d\n", channel->global_id, APP_LANE0,
						channel->global_id);
		return RC_INTEGRITY_FAIL; // Invalid channel
	}

	int16_t resp_channel_key = Get_Responder_Channel_Key_From_Channel_Key(channel_key);
	if (resp_channel_key < 0) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Respond_Is_Abort] Invalid responder channel key for channel ID: %d\n", channel->global_id, APP_LANE0,
						channel->global_id);
		return RC_INTEGRITY_FAIL; // Not a valid responder channel
	}

	responder_l0_context_t *ctx = &resp_l0_ctx[resp_channel_key];

	if (ctx->abort_event) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Respond_Is_Abort] Response transmission failed.\n", channel->global_id, APP_LANE0);
		ctx->abort_event = false; // Clear abort event flag for next round
		return RC_OK_TRUE;
	} else {
		DEBUG_LOG_TRACE(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Respond_Is_Abort] No failure detected for current response transmission.\n", channel->global_id,
						APP_LANE0);
		return RC_OK_FALSE;
	}
}

return_code_t Responder_Respond_Is_Busy(const halo_channel_t *channel) {

	if (Is_Channel_Valid(channel) != RC_OK_TRUE) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Respond_Is_Busy] Called with invalid channel pointer.\n", channel->global_id, APP_LANE0);
		return RC_INVALID_ARG; // Invalid channel pointer
	}

	if (Is_Node_Responder_For_Channel(channel) != RC_OK_TRUE) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Respond_Is_Busy] Called on channel where node %d is not the responder.\n", channel->global_id, APP_LANE0,
						node->id);
		return RC_PERM_DENIED; // Only responder node can send on responder lane
	}

	int16_t channel_key = Get_Channel_Key_From_ID(channel->global_id);
	if (channel_key < 0) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Respond_Is_Busy] Invalid channel key for channel ID: %d\n", channel->global_id, APP_LANE0,
						channel->global_id);
		return RC_INTEGRITY_FAIL; // Invalid channel
	}

	int16_t resp_channel_key = Get_Responder_Channel_Key_From_Channel_Key(channel_key);
	if (resp_channel_key < 0) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Respond_Is_Busy] Invalid responder channel key for channel ID: %d\n", channel->global_id, APP_LANE0,
						channel->global_id);
		return RC_INTEGRITY_FAIL; // Not a valid responder channel
	}

	responder_l0_context_t *ctx = &resp_l0_ctx[resp_channel_key];

	if (ctx->out_rc == RC_BUSY) {
		DEBUG_LOG_TRACE(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Respond_Is_Busy] Response still being transmitted. Responder is busy.\n", channel->global_id, APP_LANE0);
		return RC_OK_TRUE;
	} else {
		DEBUG_LOG_TRACE(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Respond_Is_Busy] No response currently being transmitted. Responder is not busy.\n", channel->global_id,
						APP_LANE0);
		return RC_OK_FALSE;
	}
}

return_code_t Responder_Receive(const halo_channel_t *channel, void *msg) {

	if (Is_Channel_Valid(channel) != RC_OK_TRUE) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Receive] Called with invalid channel pointer.\n", channel->global_id, APP_LANE1);
		return RC_INVALID_ARG; // Invalid channel pointer
	}

	if (Is_Node_Responder_For_Channel(channel) != RC_OK_TRUE) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Receive] Called on channel where node %d is not the responder.\n", channel->global_id, APP_LANE1,
						node->id);
		return RC_PERM_DENIED; // Only responder node can receive on responder lane
	}

	if (Does_Msg_Match_Channel_And_Lane(channel, APP_LANE1, msg) != RC_OK_TRUE) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Receive] Message does not match expected descriptor for this channel and lane.\n", channel->global_id,
						APP_LANE1);
		return RC_INTEGRITY_FAIL; // Message does not match expected descriptor for this channel and lane
	}

	int16_t channel_key = Get_Channel_Key_From_ID(channel->global_id);
	if (channel_key < 0) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Receive] Invalid channel key for channel ID: %d\n", channel->global_id, APP_LANE1, channel->global_id);
		return RC_INTEGRITY_FAIL; // Invalid channel
	}

	int16_t resp_channel_key = Get_Responder_Channel_Key_From_Channel_Key(channel_key);
	if (resp_channel_key < 0) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Receive] Invalid responder channel key for channel ID: %d\n", channel->global_id, APP_LANE1,
						channel->global_id);
		return RC_INTEGRITY_FAIL; // Not a valid responder channel
	}

	responder_l1_context_t *ctx = &resp_l1_ctx[resp_channel_key];

	if (ctx->abort_event) {
		ctx->abort_event = false; // Clear abort event flag for next round
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Receive] Error while receiving message.\n", channel->global_id, APP_LANE1);
		return RC_FATAL; // Previous error condition detected, cannot receive new message until resolved
	}

	if (!ctx->msg_available_event) {
		DEBUG_LOG_TRACE(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Receive] No message available to receive.\n", channel->global_id, APP_LANE1);
		return RC_OK_NO_DATA; // No message available to receive
	}

	return_code_t rc = Transport_Receive(channel_key, APP_LANE1, msg);
	if (rc == RC_OK) {
		ctx->msg_available_event = false; // Clear message available flag after receiving
		ctx->abort_event = false;		  // Clear abort event flag in case it was set during receive
		DEBUG_LOG_INFO(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Receive] Received message successfully\n", channel->global_id, APP_LANE1);
		return RC_OK;
	} else if (rc == RC_OK_NO_DATA) {
		DEBUG_LOG_TRACE(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Receive] No message available to receive\n", channel->global_id, APP_LANE1);
		return RC_OK_NO_DATA; // No message available
	} else {
		DEBUG_LOG_ERROR(APPLICATION_TAG "[RESP] [CH %d][L %d] [Responder_Receive] Failed to receive message. RC: %s\n", channel->global_id, APP_LANE1, RC_To_String(rc));
		return RC_ERROR;
	}
}

/*********************************************************************************************************************************
 * @par APPLICATION LAYER INITIALIZATION AND SERVICE
 *********************************************************************************************************************************/

return_code_t Application_Init(const halo_node_t *halo_node) {

	DEBUG_LOG_TRACE(APPLICATION_TAG "Initializing Application Layer for Node ID: %d\n", halo_node->id);

	node = halo_node;

	return_code_t rc = Transport_Init(node);
	if (rc != RC_OK) {
		DEBUG_LOG_ERROR(APPLICATION_TAG "Failed to initialize Transport layer. RC: %s\n", RC_To_String(rc));
		return rc;
	}

	return_code_t init_rc = RC_OK;

	if (node->num_responder_endpoints > 0) {
		rc = Init_Resp_L0_FSM();
		if (rc != RC_OK) { init_rc = rc; DEBUG_LOG_ERROR(APPLICATION_TAG "Failed to initialize Responder L0 FSM. RC: %s\n", RC_To_String(rc)); }
		rc = Init_Resp_L1_FSM();
		if (rc != RC_OK) { init_rc = rc; DEBUG_LOG_ERROR(APPLICATION_TAG "Failed to initialize Responder L1 FSM. RC: %s\n", RC_To_String(rc)); }
	}
	if (node->num_controller_endpoints > 0) {
		rc = Init_Ctrl_L0_FSM();
		if (rc != RC_OK) { init_rc = rc; DEBUG_LOG_ERROR(APPLICATION_TAG "Failed to initialize Controller L0 FSM. RC: %s\n", RC_To_String(rc)); }
		rc = Init_Ctrl_L1_FSM();
		if (rc != RC_OK) { init_rc = rc; DEBUG_LOG_ERROR(APPLICATION_TAG "Failed to initialize Controller L1 FSM. RC: %s\n", RC_To_String(rc)); }
	}
	rc = Init_Listener_FSM();
	if (rc != RC_OK) { init_rc = rc; DEBUG_LOG_ERROR(APPLICATION_TAG "Failed to initialize Listener FSM. RC: %s\n", RC_To_String(rc)); }

	DEBUG_LOG_INFO(APPLICATION_TAG "Application Layer initialization complete for Node ID: %d\n", halo_node->id);
	return init_rc;
}

return_code_t Controller_Sync(void) {

	if (node == NULL) {
		return RC_FATAL;
	}

	if (node->num_controller_endpoints == 0) {
		return RC_OK;
	}

	// Send the SYNC frame and return immediately. The matching FOLLOW_UP is
	// dispatched by Network_Poll() (via PAL_Service_Sync) once the SYNC transmit
	// completes — the caller must already be calling Network_Poll() each loop
	// iteration. This pattern works on both bare-metal cyclic executives and
	// preemptive RTOSes without blocking, spinning, or OS-specific primitives.
	return Network_Send_Sync();
}

return_code_t Responder_Get_Controller_Time(const halo_channel_t *channel, uint32_t *time_out) {

	if (Is_Channel_Valid(channel) != RC_OK_TRUE) {
		return RC_INTEGRITY_FAIL;
	}

	if (Is_Node_Responder_For_Channel(channel) != RC_OK_TRUE) {
		return RC_INTEGRITY_FAIL;
	}

	return Network_Get_Controller_Time((uint8_t)channel->controller->id, time_out);
}

// Local time of the last controller SYNC emission (us, 32-bit, wrap-safe).
// Drives the hidden sync cadence in Application_Service.
static uint32_t last_sync_tx_us = 0;

return_code_t Application_Service(uint8_t steps) {

	// Controller time-sync cadence. Sync emission is hidden from the application:
	// the user only pumps Service(). On controller nodes, emit one SYNC burst per
	// HALO_SYNC_PERIOD_US (the matching FOLLOW_UP rides the Network_Poll() calls
	// in the step loop below). Responder-only nodes have no controller endpoints,
	// so this is skipped; their offsets are kept fresh by the RX ISR.
	if (node->num_controller_endpoints > 0 &&
	    (uint32_t)((uint32_t)HAL_Get_Micros() - last_sync_tx_us) >= HALO_SYNC_PERIOD_US) {
		last_sync_tx_us = (uint32_t)HAL_Get_Micros();
		(void)Controller_Sync();
	}

	for (uint8_t s = 0; s < steps; s++) {

		DEBUG_LOG_TRACE(APPLICATION_TAG "Application_Service Step %d/%d\n", s + 1, steps);
		
		for (uint8_t ch = 0; ch < node->num_channels; ch++) {
			for (uint8_t lane = 0; lane < APP_NUM_LANES; lane++) {
				Transport_Poll(ch, lane);
			}
		}

		for (uint8_t ch = 0; ch < node->num_channels; ch++) {
			if (node->channels[ch]->responder->id == node->id) {
				// This is a responder channel, poll responder FSM for lane 0
				uint8_t resp_channel_key = Get_Responder_Channel_Key_From_Channel_Key(ch);
				Responder_L0_FSM_Poll(resp_channel_key, APP_LANE0, NULL, false);
				Responder_L1_FSM_Poll(resp_channel_key, APP_LANE1);
			}

			if (node->channels[ch]->controller->id == node->id) {
				// This is a controller channel, poll controller FSM for lane 0
				uint8_t ctrl_channel_key = Get_Controller_Channel_Key_From_Channel_Key(ch);
				Controller_L0_FSM_Poll(ctrl_channel_key, false);
				Controller_L1_FSM_Poll(ctrl_channel_key, NULL, false);
			}

			if (Is_Node_Listener_For_Channel(node->channels[ch]) == RC_OK_TRUE) {
				uint8_t lst_key = (uint8_t)Get_Listener_Channel_Key_From_Channel_Key(ch);
				for (uint8_t lane = 0; lane < APP_NUM_LANES; lane++) {
					Listener_FSM_Poll(lst_key, lane);
				}
			}
		}
	}
	return RC_OK;
}