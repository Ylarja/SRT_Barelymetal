#include "../Include/PAL.h"
#include "../Private/PAL_CAN_Config.h"
#include "../Private/PAL_CAN_Mailbox.h"
#include <stdlib.h>

#define PAL_TAG "[PAL] "

// Upper bound on distinct controllers we install hardware sync-acceptance
// filters for. Scratch-array cap only; the real limit is the HAL's filter-slot
// count — HAL_CAN_Add_Filter returns RC_NO_RESOURCE when slots run out.

static inline return_code_t PAL_Pack_Message(lifo_buffer_t *pbuf, can_msg_t *msg) {

	if (pbuf == NULL || msg == NULL) {
		return RC_INVALID_ARG;
	}

	uint8_t stored_cnt = 0;
	(void)LIFO_Buffer_Get_Stored_Count(pbuf, &stored_cnt);
	if (stored_cnt < CAN_ID_TOTAL_BYTES + CAN_DLC_BYTES) {
		return RC_INTEGRITY_FAIL;
	}

	uint8_t temp_buffer = 0;

	(void)LIFO_Buffer_Pop(pbuf, &temp_buffer);
	msg->id = (uint32_t)(temp_buffer & CAN_SRC_DST_ID_MASK) << CAN_SRC_ID_SHIFT; // Packing SRC ID

	(void)LIFO_Buffer_Pop(pbuf, &temp_buffer);
	msg->id |= (uint32_t)(temp_buffer & CAN_SRC_DST_ID_MASK) << CAN_DST_ID_SHIFT; // Packing DST ID

	(void)LIFO_Buffer_Pop(pbuf, &temp_buffer);
	msg->id |= (uint32_t)(temp_buffer & CAN_CHANNEL_ID_MASK) << CAN_CHANNEL_ID_SHIFT; // Packing CHANNEL ID

	(void)LIFO_Buffer_Pop(pbuf, &temp_buffer);
	msg->id |= (uint32_t)(temp_buffer & CAN_LANE_ID_MASK) << CAN_LANE_ID_SHIFT; // Packing LANE ID

	(void)LIFO_Buffer_Pop(pbuf, &temp_buffer);
	msg->id |= (uint32_t)(temp_buffer & CAN_PACKET_ID_MASK) << CAN_PACKET_ID_SHIFT; // Packing PACKET ID

	(void)LIFO_Buffer_Pop(pbuf, &temp_buffer);
	msg->id |= (uint32_t)(temp_buffer & CAN_RETRANS_ID_MASK) << CAN_RETRANS_ID_SHIFT; // Packing RETRANS ID

	(void)LIFO_Buffer_Pop(pbuf, &temp_buffer); // Packing TAG
	if ((temp_buffer & (uint8_t)~CAN_TAG_MASK) != 0) {
		return RC_INTEGRITY_FAIL;
	}
	msg->id |= (uint32_t)(temp_buffer & CAN_TAG_MASK) << CAN_TAG_SHIFT;

	// SYNC_FRAME is active-low: 0=sync, 1=data. Set it for all normal data frames.
	msg->id |= (1UL << CAN_SYNC_FRAME_SHIFT);

	(void)LIFO_Buffer_Pop(pbuf, &temp_buffer); // Packing DLC
	if(temp_buffer > CAN_MAX_DLC || temp_buffer == 0){
		return RC_INTEGRITY_FAIL;
	}
	msg->dlc = temp_buffer;

	LIFO_Buffer_Get_Stored_Count(pbuf, &stored_cnt);
	if (stored_cnt < msg->dlc) {
		return RC_INTEGRITY_FAIL;
	}

	(void)LIFO_Buffer_Pop_N(pbuf, msg->data, msg->dlc); // Packing Data

	LIFO_Buffer_Clear(pbuf); // Clear buffer after packing, defensive measure

	return RC_OK;
}

static inline return_code_t PAL_Unpack_Message(const can_msg_t *msg, lifo_buffer_t *pbuf) {

	if (pbuf == NULL || msg == NULL) {
		return RC_INVALID_ARG;
	}

	LIFO_Buffer_Clear(pbuf); // Clear buffer before unpacking, defensive measure

	uint8_t temp_buffer = 0;

	if(msg->dlc > CAN_MAX_DLC){
		return RC_INTEGRITY_FAIL;
	}

	(void)LIFO_Buffer_Push_N(pbuf, msg->data, msg->dlc); // Unpacking Data

	temp_buffer = (msg->dlc);
	(void)LIFO_Buffer_Push(pbuf, &temp_buffer); // Unpacking DLC

	temp_buffer = (msg->id >> CAN_TAG_SHIFT) & CAN_TAG_MASK; // Unpacking TAG
	(void)LIFO_Buffer_Push(pbuf, &temp_buffer);

	temp_buffer = (msg->id >> CAN_RETRANS_ID_SHIFT) & CAN_RETRANS_ID_MASK; // Unpacking RETRANS ID
	(void)LIFO_Buffer_Push(pbuf, &temp_buffer);

	temp_buffer = (msg->id >> CAN_PACKET_ID_SHIFT) & CAN_PACKET_ID_MASK; // Unpacking PACKET ID
	
	DEBUG_LOG_TRACE("Unpacking packet with ID: %d from received message\n", temp_buffer);

	(void)LIFO_Buffer_Push(pbuf, &temp_buffer);

	temp_buffer = (msg->id >> CAN_LANE_ID_SHIFT) & CAN_LANE_ID_MASK; // Unpacking LANE ID
	(void)LIFO_Buffer_Push(pbuf, &temp_buffer);

	temp_buffer = (msg->id >> CAN_CHANNEL_ID_SHIFT) & CAN_CHANNEL_ID_MASK; // Unpacking CHANNEL ID
	(void)LIFO_Buffer_Push(pbuf, &temp_buffer);

	temp_buffer = (msg->id >> CAN_DST_ID_SHIFT) & CAN_SRC_DST_ID_MASK; // Unpacking DST ID
	(void)LIFO_Buffer_Push(pbuf, &temp_buffer);

	temp_buffer = (msg->id >> CAN_SRC_ID_SHIFT) & CAN_SRC_DST_ID_MASK; // Unpacking SRC ID
	(void)LIFO_Buffer_Push(pbuf, &temp_buffer);

	return RC_OK;
}

return_code_t PAL_Receive(lifo_buffer_t *pbuf, uint16_t channel_id, uint8_t lane_id, uint32_t *rx_timestamp_us) {

	can_msg_t msg = {0};
	return_code_t rc;
	rc = PAL_CAN_Mailbox_Pop(channel_id, lane_id, &msg, rx_timestamp_us);
	switch (rc) {
	case RC_OK:
		(void)PAL_Unpack_Message(&msg, pbuf);
		return rc;
	case RC_OK_NO_DATA:
		return rc;
	default:
		return RC_FATAL;
	}

}

return_code_t PAL_Is_Data_Available(uint16_t channel_id, uint8_t lane_id) {

	if(PAL_CAN_Mailbox_Is_Empty(channel_id, lane_id) == RC_OK_FALSE){
		return RC_OK_TRUE;
	} else {
		return RC_OK_FALSE;
	}
}

return_code_t PAL_Send(lifo_buffer_t *pbuf) {
	can_msg_t msg = {0};
	return_code_t rc;

	rc = PAL_Pack_Message(pbuf, &msg);
	if (rc != RC_OK) {
		DEBUG_LOG_ERROR(PAL_TAG "PAL_Pack_Message failed. RC: %s\n", RC_To_String(rc));
		return rc;
	}
	rc = HAL_CAN_Send(&msg);

	return rc;

}

return_code_t PAL_Platform_Init(const halo_node_t *node_desc) {

	if (node_desc == NULL) {
		DEBUG_LOG_ERROR(PAL_TAG "NULL node descriptor passed to PAL_Platform_Init.\n");
		return RC_FATAL;
	}

	uint16_t *channel_id_list = NULL;
	channel_id_list = malloc(node_desc->num_channels * sizeof(uint16_t));
	if (channel_id_list == NULL) {
		DEBUG_LOG_ERROR(PAL_TAG "Failed to allocate memory for channel ID list.\n");
		return RC_FATAL;
	}

	DEBUG_LOG_TRACE(PAL_TAG "Channels ID list allocated successfully. Populating channel ID list.\n");

	for (uint8_t i = 0; i < node_desc->num_channels; i++) {
		channel_id_list[i] = node_desc->channels[i]->global_id;
	}
	//PAL_Init_Remote_Endpoints(node_id, channel_ids);
	PAL_CAN_Mailbox_Init(channel_id_list, node_desc->num_channels);
	free(channel_id_list);
	HAL_Virtual_Serial_Init(PAL_VIRTUAL_SERIAL_BAUDRATE);
	// Canale "osservato" = il nodo non è né controller né responder
	bool has_observed_channel = false;
	for (uint8_t i = 0; i < node_desc->num_channels; i++) {
		if (node_desc->channels[i]->controller->id != node_desc->id &&
		    node_desc->channels[i]->responder->id != node_desc->id) {
			has_observed_channel = true;
			break;
		}
	}
	// Listen-only solo per un nodo puramente passivo (un nodo misto deve poter trasmettere)
	const bool passive_only = has_observed_channel &&
	                          node_desc->num_controller_endpoints == 0 &&
	                          node_desc->num_responder_endpoints == 0;
	
	uint32_t hardware_filter_id;
	uint32_t hardware_filter_mask;
	if (passive_only) {
		// Listener puro: accetta TUTTI i frame estesi, anche quelli di nodi esterni alla rete.
		// La classificazione (canale noto / sync valido / esterno) la fa l'handler della mailbox.
		hardware_filter_id   = 0;
		hardware_filter_mask = 0;
	} else if (has_observed_channel) {
		hardware_filter_id   = (1UL << CAN_SYNC_FRAME_SHIFT);   // dato, DST qualunque
		hardware_filter_mask = CAN_LISTENER_DATA_FILTER_MASK;
	} else {
		hardware_filter_id   = ((node_desc->id & CAN_SRC_DST_ID_MASK) << CAN_DST_ID_SHIFT)
		                     | (1UL << CAN_SYNC_FRAME_SHIFT);
		hardware_filter_mask = CAN_HARDWARE_FILTER_MASK;
	}

	
	HAL_CAN_Init_Hardware(CAN_DEFAULT_BAUDRATE, hardware_filter_id, hardware_filter_mask, PAL_RX_MODE_INTERRUPT);
	// Register TX sync callback so PAL is notified when the SYNC frame is on the wire.
	return_code_t tx_rc = PAL_Register_Sync_TX_Callback();
	if (tx_rc != RC_OK) {
		DEBUG_LOG_ERROR(PAL_TAG "Could not register TX sync callback (RC: %s). Two-step sync unavailable.\n", RC_To_String(tx_rc));
	}

	DEBUG_LOG_TRACE(PAL_TAG "Layer initialized for Node ID: %d, hardware filter ID: %x, hardware filter mask: %x\n", node_desc->id, hardware_filter_id, hardware_filter_mask);

// Controller da cui questo nodo accetta sync: uno per ogni canale di cui non e' controller.
	uint8_t known_ctrl_ids[16];
	uint8_t known_count = 0;
	for (uint8_t i = 0; i < node_desc->num_channels; i++) {
		if (node_desc->channels[i]->controller->id != node_desc->id) {
			uint8_t ctrl_id = (uint8_t)node_desc->channels[i]->controller->id;
			bool already_listed = false;
			for (uint8_t j = 0; j < known_count; j++) {
				if (known_ctrl_ids[j] == ctrl_id) { already_listed = true; break; }
			}
			if (!already_listed && known_count < 16) {
				known_ctrl_ids[known_count++] = ctrl_id;
			}
		}
	}

	if (passive_only) {
		// Filtro gia' aperto: i filtri di sync non servono. Attiva il tracciamento degli ID esterni.
		return_code_t trc = PAL_CAN_Mailbox_Enable_Unknown_Tracking(known_ctrl_ids, known_count);
		if (trc != RC_OK) {
			DEBUG_LOG_ERROR(PAL_TAG "Could not enable external-ID tracking (RC: %s).\n", RC_To_String(trc));
		}
	} else {
		// Un filtro di sync per ogni controller: SRC = ctrl_id, SYNC_FRAME = 0 (active-low).
		for (uint8_t j = 0; j < known_count; j++) {
			uint32_t sync_filter_id = ((uint32_t)known_ctrl_ids[j] << CAN_SRC_ID_SHIFT);
			return_code_t frc = HAL_CAN_Add_Filter(sync_filter_id, CAN_SYNC_FILTER_MASK);
			if (frc != RC_OK) {
				DEBUG_LOG_ERROR(PAL_TAG "Could not install sync filter for ctrl ID %d (RC: %s).\n", known_ctrl_ids[j], RC_To_String(frc));
			} else {
				DEBUG_LOG_TRACE(PAL_TAG "Installed sync filter for ctrl ID %d (SRC=%d, SYNC_FRAME=0).\n", known_ctrl_ids[j], known_ctrl_ids[j]);
			}
		}
	}

	return RC_OK;
}

return_code_t PAL_Mailbox_Peek_Nth(lifo_buffer_t *pbuf, uint16_t channel_id, uint8_t lane_id, uint8_t n) {

	can_msg_t msg = {0};
	return_code_t rc;

	rc = PAL_CAN_Mailbox_Peek_Nth(channel_id, lane_id, &msg, n);

	switch (rc) {
	case RC_OK:
		(void)PAL_Unpack_Message(&msg, pbuf);
		return rc;
	case RC_OK_NO_DATA:
		return rc;
	default:
		return RC_FATAL;
	}
}

return_code_t PAL_Mailbox_Clear(uint16_t channel_id, uint8_t lane_id) {

	return PAL_CAN_Mailbox_Clear(channel_id, lane_id);
}

return_code_t PAL_Mailbox_Dump(uint16_t channel_id, uint8_t lane_id) {

	return PAL_CAN_Mailbox_Dump(channel_id, lane_id);
}

return_code_t PAL_Get_Max_Payload_Bytes(uint8_t *payload_size) {

	if (payload_size == NULL) {
		return RC_INVALID_ARG;
	}

	*payload_size = CAN_MAX_DLC;

	return RC_OK;
}

// Pending follow-up state. Set from HAL TX ISR callback; consumed in task context.
// uint32_t is atomically readable/writable on 32-bit Cortex-M — no torn-read risk.
static volatile bool     follow_up_pending   = false;
static volatile uint32_t follow_up_timestamp = 0;
static          uint8_t  follow_up_src_id    = 0;
static          uint8_t  follow_up_dst_id    = 0;
static          uint8_t  follow_up_seq       = 0;

// HAL TX complete ISR callback — must be minimal.
static void PAL_On_Sync_TX_Complete(uint32_t timestamp_us) {
	follow_up_timestamp = timestamp_us;
	follow_up_pending   = true;
}

return_code_t PAL_Send_Sync(uint8_t src_id, uint8_t dst_id, uint8_t seq) {

	can_msg_t msg = {0};

	msg.id = ((uint32_t)(src_id & CAN_SRC_DST_ID_MASK) << CAN_SRC_ID_SHIFT)
	       | ((uint32_t)(dst_id & CAN_SRC_DST_ID_MASK) << CAN_DST_ID_SHIFT)
	       | ((uint32_t)(seq    & CAN_PACKET_ID_MASK)   << CAN_PACKET_ID_SHIFT)
	       | ((uint32_t)CAN_SYNC_TYPE_SYNC              << CAN_RETRANS_ID_SHIFT);  // SYNC_FRAME bit = 0 (active-low)

	msg.dlc    = 1;   // DLC=0 may not be supported by all transceivers
	msg.data[0] = 0;

	// Store follow-up context before sending so the TX ISR callback
	// can find it without any upward call.
	follow_up_src_id = src_id;
	follow_up_dst_id = dst_id;
	follow_up_seq    = seq;
	follow_up_pending = false; // clear any stale pending flag

	return HAL_CAN_Send_Sync(&msg);
}

return_code_t PAL_Service_Sync(void) {

	if (!follow_up_pending) {
		return RC_OK_NO_DATA; // Nothing pending — caller can spin on this
	}

	// Single volatile uint32_t read — atomic on Cortex-M, no retry needed.
	uint32_t t    = follow_up_timestamp;
	follow_up_pending = false;

	can_msg_t msg = {0};

	msg.id = ((uint32_t)(follow_up_src_id & CAN_SRC_DST_ID_MASK) << CAN_SRC_ID_SHIFT)
	       | ((uint32_t)(follow_up_dst_id & CAN_SRC_DST_ID_MASK) << CAN_DST_ID_SHIFT)
	       | ((uint32_t)(follow_up_seq    & CAN_PACKET_ID_MASK)   << CAN_PACKET_ID_SHIFT)
	       | ((uint32_t)CAN_SYNC_TYPE_FOLLOW_UP                   << CAN_RETRANS_ID_SHIFT);  // SYNC_FRAME bit = 0 (active-low)

	msg.dlc = 4;
	msg.data[0] = (uint8_t)( t        & 0xFF);
	msg.data[1] = (uint8_t)((t >>  8) & 0xFF);
	msg.data[2] = (uint8_t)((t >> 16) & 0xFF);
	msg.data[3] = (uint8_t)((t >> 24) & 0xFF);

	return HAL_CAN_Send(&msg); // Normal send — no TX ISR needed for FOLLOW_UP
}

return_code_t PAL_Register_Sync_TX_Callback(void) {
	return HAL_CAN_Register_TX_Sync_Callback(PAL_On_Sync_TX_Complete);
}

return_code_t PAL_Register_Sync_Callback(pal_sync_rx_callback_t cb) {
	return PAL_CAN_Mailbox_Register_Sync_Callback(cb);
}

return_code_t PAL_Register_Data_Frame_Callback(pal_data_rx_callback_t cb) {
	return PAL_CAN_Mailbox_Register_Data_Callback(cb);
}
return_code_t PAL_Take_Unknown_Ids(pal_unknown_id_t *out, uint8_t max,
                                   uint8_t *count, uint32_t *untracked_frames) {
	return PAL_CAN_Mailbox_Take_Unknown_Ids(out, max, count, untracked_frames);
}