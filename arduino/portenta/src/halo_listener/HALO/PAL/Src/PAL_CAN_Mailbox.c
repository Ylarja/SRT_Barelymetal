
#include "../Private/PAL_CAN_Mailbox.h"
//#include "../../Core/Core.h"
#include "../Private/PAL_CAN_Config.h"
#include "../Private/PAL_Config.h"
#include <stdlib.h>


#define NUM_LANES 2
typedef struct {
	can_msg_t msg;
	uint32_t  rx_timestamp_us;   // istante di arrivo locale (us, 32 bit), da HAL_CAN_Get_RX_Timestamp()
} pal_rx_frame_t;

struct can_mailbox {
	pal_rx_frame_t can_rb_storage[PAL_RX_MAILBOX_DEPTH];
	ring_buffer_t can_rb;
	uint16_t channel_id;
	uint8_t lane_id;
};

static can_mailbox_t *mailboxes = NULL;
static uint8_t num_mailboxes = 0;
static pal_sync_rx_callback_t sync_rx_callback = NULL;
static pal_data_rx_callback_t data_rx_callback = NULL;

/* ---------- Tracciamento degli ID esterni (solo nodi listener) ---------- */
#define PAL_KNOWN_SYNC_SRC_MAX 16

static bool             unknown_tracking_enabled = false;
static uint8_t          known_sync_src[PAL_KNOWN_SYNC_SRC_MAX];
static uint8_t          num_known_sync_src = 0;
static pal_unknown_id_t unknown_ids[PAL_UNKNOWN_ID_MAX];
static uint8_t          num_unknown_ids = 0;
static uint32_t         untracked_frames = 0;   // frame di ID che non sono entrati in tabella

// Registra un frame esterno: incrementa il contatore del suo ID, oppure aggiunge l'ID.
// Chiamata dal contesto di ricezione (sulla ZCU: il thread RX).
static void PAL_CAN_Mailbox_Record_Unknown(uint32_t can_id) {
	CRITICAL_SECTION_ENTER();
	uint8_t i;
	for (i = 0; i < num_unknown_ids; i++) {
		if (unknown_ids[i].can_id == can_id) {
			unknown_ids[i].frames++;
			break;
		}
	}
	if (i == num_unknown_ids) {              // ID non ancora in tabella
		if (num_unknown_ids < PAL_UNKNOWN_ID_MAX) {
			unknown_ids[num_unknown_ids].can_id = can_id;
			unknown_ids[num_unknown_ids].frames = 1;
			num_unknown_ids++;
		} else {
			untracked_frames++;
		}
	}
	CRITICAL_SECTION_EXIT();
}

static bool PAL_CAN_Mailbox_Is_Known_Sync_Source(uint8_t src_id) {
	for (uint8_t i = 0; i < num_known_sync_src; i++) {
		if (known_sync_src[i] == src_id) {
			return true;
		}
	}
	return false;
}

return_code_t PAL_CAN_Mailbox_Register_Sync_Callback(pal_sync_rx_callback_t cb) {
	sync_rx_callback = cb;
	return RC_OK;
}

return_code_t PAL_CAN_Mailbox_Register_Data_Callback(pal_data_rx_callback_t cb) {
	data_rx_callback = cb;
	return RC_OK;
}

static inline return_code_t PAL_CAN_Mailbox_Compute_Global_Channel_ID(uint32_t msg_id, uint16_t *global_channel_id) {

	if (global_channel_id == NULL) {
		return RC_INVALID_ARG;
	}
	uint8_t src_id = (msg_id >> CAN_SRC_ID_SHIFT) & CAN_SRC_DST_ID_MASK;
	uint8_t dst_id = (msg_id >> CAN_DST_ID_SHIFT) & CAN_SRC_DST_ID_MASK;
	uint8_t local_channel_id = (msg_id >> CAN_CHANNEL_ID_SHIFT) & CAN_CHANNEL_ID_MASK;

	uint8_t low_id = (src_id < dst_id) ? src_id : dst_id;
	uint8_t high_id = (src_id > dst_id) ? src_id : dst_id;
	// low_id, high_id and local channel_id are all 5 bits
	*global_channel_id = ((uint16_t)low_id << CAN_GLOBAL_CHANNEL_ID_LOW_ID_SHIFT) | ((uint16_t)high_id << CAN_GLOBAL_CHANNEL_ID_HIGH_ID_SHIFT) | (uint16_t)local_channel_id;

	return RC_OK;
}

void PAL_CAN_Rx_Mailbox_IRQHandler() {

	can_msg_t msg;
	while (HAL_CAN_Receive(&msg) == RC_OK) {
		uint32_t rx_timestamp = HAL_CAN_Get_RX_Timestamp();

		// SYNC_FRAME bit is active-low: 0 = sync frame, 1 = data frame.
		uint8_t sync_flag = (uint8_t)((msg.id >> CAN_SYNC_FRAME_SHIFT) & CAN_SYNC_FRAME_MASK);
		if (!sync_flag) {
			
			uint8_t src_id = (uint8_t)((msg.id >> CAN_SRC_ID_SHIFT) & CAN_SRC_DST_ID_MASK);

			// Listener: un "sync" da una sorgente che non e' un controller noto e' un frame esterno.
			if (unknown_tracking_enabled && !PAL_CAN_Mailbox_Is_Known_Sync_Source(src_id)) {
				PAL_CAN_Mailbox_Record_Unknown(msg.id);
				continue;
			}
			if (sync_rx_callback != NULL) {
				uint8_t seq       = (uint8_t)((msg.id >> CAN_PACKET_ID_SHIFT)  & CAN_PACKET_ID_MASK);
				uint8_t sync_type = (uint8_t)((msg.id >> CAN_RETRANS_ID_SHIFT) & CAN_RETRANS_ID_MASK);
				uint32_t timestamp = 0;
				if (sync_type == CAN_SYNC_TYPE_FOLLOW_UP) {
					// FOLLOW_UP carries the controller's TX timestamp in the payload.
					if (msg.dlc >= 4) {
						timestamp = (uint32_t)msg.data[0]
						          | ((uint32_t)msg.data[1] << 8)
						          | ((uint32_t)msg.data[2] << 16)
						          | ((uint32_t)msg.data[3] << 24);
					}
				} else if (sync_type == CAN_SYNC_TYPE_SYNC) {
					// SYNC: local arrival time, taken from the generic HAL RX-timestamp
					// interface (captured at interrupt time by the board's HAL) rather
					// than a Get_Micros() call here — which on a deferred-RX platform
					// would be thread-dispatch time, not arrival time.
					timestamp = rx_timestamp;
				}
				sync_rx_callback(src_id, seq, sync_type, timestamp);
			}
		} else {
			if (data_rx_callback != NULL) {
				uint8_t src_id = (uint8_t)((msg.id >> CAN_SRC_ID_SHIFT) & CAN_SRC_DST_ID_MASK);
				uint8_t dst_id = (uint8_t)((msg.id >> CAN_DST_ID_SHIFT) & CAN_SRC_DST_ID_MASK);
				uint8_t local_ch = (uint8_t)((msg.id >> CAN_CHANNEL_ID_SHIFT) & CAN_CHANNEL_ID_MASK);
				uint8_t lane = (uint8_t)((msg.id >> CAN_LANE_ID_SHIFT) & CAN_LANE_ID_MASK);
				uint8_t packet_id = (uint8_t)((msg.id >> CAN_PACKET_ID_SHIFT) & CAN_PACKET_ID_MASK);
				uint8_t tag = (uint8_t)((msg.id >> CAN_TAG_SHIFT) & CAN_TAG_MASK);
				data_rx_callback(src_id, dst_id, local_ch, lane, packet_id, tag, rx_timestamp);
			}
			return_code_t rc = PAL_CAN_Mailbox_Push(&msg,rx_timestamp);
				if (rc == RC_INTEGRITY_FAIL &&
			    unknown_tracking_enabled) {
				PAL_CAN_Mailbox_Record_Unknown(msg.id);
			}
		}
	}
}

return_code_t PAL_CAN_Mailbox_Init(uint16_t *channel_id_list, uint8_t num_channels) {

	if (channel_id_list == NULL) {
		return RC_INVALID_ARG;
	}

	return_code_t rc;

	num_mailboxes = num_channels * NUM_LANES;
	mailboxes = (can_mailbox_t *)malloc(sizeof(can_mailbox_t) * num_mailboxes);
	if (mailboxes == NULL) {
		return RC_FATAL;
	}

	for (int j = 0; j < num_channels; j++) {
		for (int k = 0; k < NUM_LANES; k++) {
			mailboxes[j * NUM_LANES + k].channel_id = channel_id_list[j];
			mailboxes[j * NUM_LANES + k].lane_id = k;
			
			ring_buffer_t *rb = &mailboxes[j * NUM_LANES + k].can_rb;
			pal_rx_frame_t *storage = mailboxes[j * NUM_LANES + k].can_rb_storage;
			rc = Ring_Buffer_Init(rb, storage, PAL_RX_MAILBOX_DEPTH, sizeof(pal_rx_frame_t));

			if (rc != RC_OK) {
				// LOG rc
				return RC_FATAL;
			}
			DEBUG_LOG_TRACE("Initialized mailbox for channel_id %d, lane_id %d", mailboxes[j * NUM_LANES + k].channel_id, mailboxes[j * NUM_LANES + k].lane_id);
		}
	}

	rc = HAL_CAN_Register_Callback(PAL_CAN_Rx_Mailbox_IRQHandler);

	if (rc != RC_OK) {
		// LOG rc
		return RC_FATAL;
	}

	return RC_OK;
}

return_code_t PAL_CAN_Mailbox_Push(const can_msg_t *msg,uint32_t rx_timestamp_us) {

	if (msg == NULL) {
		return RC_INVALID_ARG;
	}

	uint16_t channel_id;
	(void)PAL_CAN_Mailbox_Compute_Global_Channel_ID(msg->id, &channel_id);

	uint8_t lane_id = (msg->id >> CAN_LANE_ID_SHIFT) & CAN_LANE_ID_MASK;
	
	pal_rx_frame_t frame;
	frame.msg = *msg;
	frame.rx_timestamp_us = rx_timestamp_us;

	return_code_t rc;

	for (int i = 0; i < num_mailboxes; i++) {
		if (channel_id == mailboxes[i].channel_id && lane_id == mailboxes[i].lane_id) {
			rc = Ring_Buffer_Push(&mailboxes[i].can_rb, &frame);
			if (RC_SUCCESS(rc)) {
				return rc;
			} else if (rc == RC_NO_RESOURCE) {
				return rc;
			} else {
				return RC_FATAL;
			}
		}
	}
	return RC_INTEGRITY_FAIL;
}

return_code_t PAL_CAN_Mailbox_Pop(uint16_t channel_id, uint8_t lane_id, can_msg_t *msg,uint32_t *rx_timestamp_us) {

	if (msg == NULL) {
		return RC_INVALID_ARG;
	}

	return_code_t rc;

	for (int i = 0; i < num_mailboxes; i++) {
		if (channel_id == mailboxes[i].channel_id && lane_id == mailboxes[i].lane_id) {
			pal_rx_frame_t frame;
			rc = Ring_Buffer_Pop(&mailboxes[i].can_rb, &frame);
			
				if (rc == RC_OK) {
				*msg = frame.msg;
				if (rx_timestamp_us != NULL) {
					*rx_timestamp_us = frame.rx_timestamp_us;
				}
				return RC_OK;
			} else if (rc == RC_OK_NO_DATA) {
				return rc;
			}else {
				return RC_FATAL;
			}
		}
	}
	return RC_INTEGRITY_FAIL; // channel_id not found
}

return_code_t PAL_CAN_Mailbox_Is_Empty(uint16_t channel_id, uint8_t lane_id) {

	return_code_t rc;

	for (int i = 0; i < num_mailboxes; i++) {
		if (channel_id == mailboxes[i].channel_id && lane_id == mailboxes[i].lane_id) {
			rc = Ring_Buffer_Is_Empty(&mailboxes[i].can_rb);
			if (RC_SUCCESS(rc)) {
				return rc;
			} else {
				return RC_FATAL;
			}
		}
	}
	return RC_INTEGRITY_FAIL; // channel_id not found
}

return_code_t PAL_CAN_Mailbox_Is_Full(uint16_t channel_id, uint8_t lane_id) {

	return_code_t rc;

	for (int i = 0; i < num_mailboxes; i++) {
		if (channel_id == mailboxes[i].channel_id && lane_id == mailboxes[i].lane_id) {
			rc = Ring_Buffer_Is_Full(&mailboxes[i].can_rb);
			if (RC_SUCCESS(rc)) {
				return rc;
			} else {
				return RC_FATAL;
			}
		}
	}
	return RC_INTEGRITY_FAIL; // channel_id not found
}

return_code_t PAL_CAN_Mailbox_Peek_Nth(uint16_t channel_id, uint8_t lane_id, can_msg_t *msg, uint8_t n) {

	if (msg == NULL) {
		return RC_INVALID_ARG;
	}

	if (n == 0) {
		return RC_DATA_RANGE;
	}

	return_code_t rc;

	for (int i = 0; i < num_mailboxes; i++) {
		if (channel_id == mailboxes[i].channel_id && lane_id == mailboxes[i].lane_id) {
			pal_rx_frame_t frame;
			rc = Ring_Buffer_Peek_Nth(&mailboxes[i].can_rb, n, &frame);
			if (rc == RC_OK) {
				*msg = frame.msg;
			}
			if (RC_SUCCESS(rc)) {
				return rc;
			} else {
				return RC_FATAL;
			}
		}
	}
	return RC_INTEGRITY_FAIL; // channel_id not found
}

return_code_t PAL_CAN_Mailbox_Clear(uint16_t channel_id, uint8_t lane_id) {

	return_code_t rc;

	for (int i = 0; i < num_mailboxes; i++) {
		if (channel_id == mailboxes[i].channel_id && lane_id == mailboxes[i].lane_id) {
			rc = Ring_Buffer_Clear(&mailboxes[i].can_rb);
			if (RC_SUCCESS(rc)) {
				return rc;
			} else {
				return RC_FATAL;
			}
		}
	}
	return RC_INTEGRITY_FAIL; // channel_id not found
}

/// DA MODIFICARE LETTURA CANALE GLOBALE

return_code_t PAL_CAN_Mailbox_Dump(uint16_t channel_id, uint8_t lane_id) {

	can_msg_t msg;
	return_code_t rc;

	char header[50];
	snprintf(header, sizeof(header), "Dumping Mailbox for Channel ID %d, Lane ID %d:\n", channel_id, lane_id);
	HAL_Virtual_Serial_Write(header);

	if (PAL_CAN_Mailbox_Is_Empty(channel_id, lane_id) == RC_OK_TRUE) {

		HAL_Virtual_Serial_Write("No messages in mailbox.\n\n");

		return RC_OK;
	}

	for (int j = 0; j < PAL_RX_MAILBOX_DEPTH; j++) {
		rc = PAL_CAN_Mailbox_Peek_Nth(channel_id, lane_id, &msg, j + 1);
		if (!RC_SUCCESS(rc)) {
			return RC_FATAL;
		}

		if (rc == RC_OK_NO_DATA) {
			break;
		}

		char message[100];
		uint8_t msg_src = (msg.id >> CAN_SRC_ID_SHIFT) & CAN_SRC_DST_ID_MASK;
		uint8_t msg_dst = (msg.id >> CAN_DST_ID_SHIFT) & CAN_SRC_DST_ID_MASK;
		uint8_t msg_channel = (msg.id >> CAN_CHANNEL_ID_SHIFT) & CAN_CHANNEL_ID_MASK;
		uint8_t msg_lane = (msg.id >> CAN_LANE_ID_SHIFT) & CAN_LANE_ID_MASK;
		uint8_t msg_packet = (msg.id >> CAN_PACKET_ID_SHIFT) & CAN_PACKET_ID_MASK;
		uint8_t msg_retrans = (msg.id >> CAN_RETRANS_ID_SHIFT) & CAN_RETRANS_ID_MASK;

		snprintf(message, sizeof(message), "Msg 0x%02X: SRC=0x%02X DST=0x%02X CH=0x%02X LN=0x%02X PKT=0x%02X RETR=0x%02X DLC=%d DATA=[", j + 1, msg_src, msg_dst, msg_channel,
				 msg_lane, msg_packet, msg_retrans, msg.dlc);

		for (int k = 0; k < msg.dlc; k++) {
			char byte_str[5];
			snprintf(byte_str, sizeof(byte_str), "0x%02X", msg.data[k]);
			strcat(message, byte_str);
			if (k < msg.dlc - 1) {
				strcat(message, ", ");
			}
		}

		HAL_Virtual_Serial_Write(message);
		HAL_Virtual_Serial_Write("]\n");
	}

	HAL_Virtual_Serial_Write("\n");

	return RC_OK;
}
return_code_t PAL_CAN_Mailbox_Enable_Unknown_Tracking(const uint8_t *known_src, uint8_t n) {

	if ((known_src == NULL && n > 0) || n > PAL_KNOWN_SYNC_SRC_MAX) {
		return RC_INVALID_ARG;
	}

	CRITICAL_SECTION_ENTER();
	for (uint8_t i = 0; i < n; i++) {
		known_sync_src[i] = known_src[i];
	}
	num_known_sync_src = n;
	num_unknown_ids = 0;
	untracked_frames = 0;
	CRITICAL_SECTION_EXIT();

	// Ultima istruzione: da qui in poi l'handler di ricezione inizia a registrare.
	unknown_tracking_enabled = true;
	return RC_OK;
}

return_code_t PAL_CAN_Mailbox_Take_Unknown_Ids(pal_unknown_id_t *out, uint8_t max,
                                               uint8_t *count, uint32_t *untracked_out) {

	if (out == NULL || count == NULL || untracked_out == NULL) {
		return RC_INVALID_ARG;
	}

	*count = 0;
	*untracked_out = 0;
	if (!unknown_tracking_enabled) {
		return RC_OK_NO_DATA;
	}

	// Copia e azzeramento nella stessa sezione critica: nessun frame puo' andare perso
	// tra la lettura e la pulizia della tabella.
	CRITICAL_SECTION_ENTER();
	uint8_t  n = 0;
	uint32_t extra = untracked_frames;
	for (uint8_t i = 0; i < num_unknown_ids; i++) {
		if (n < max) {
			out[n++] = unknown_ids[i];
		} else {
			extra += unknown_ids[i].frames;
		}
	}
	num_unknown_ids  = 0;
	untracked_frames = 0;
	CRITICAL_SECTION_EXIT();

	*count = n;
	*untracked_out = extra;
	return RC_OK;
}