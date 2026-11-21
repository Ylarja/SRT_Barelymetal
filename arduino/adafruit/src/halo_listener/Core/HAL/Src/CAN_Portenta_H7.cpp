#include "../../Core_Config.h"

#ifdef __TARGET_DEVICE_PORTENTA_H7__

#include "../Include/CAN.h"

#include "mbed.h"

// FDCAN RX FIFO0 depth (default mbed: 8, max with default RAM layout: ~59)
#define FDCAN_RX_FIFO0_DEPTH 32

// Subclass to access the protected can_t member for FDCAN reconfiguration
class HAL_CAN : public mbed::CAN {
public:
    using CAN::CAN;
    FDCAN_HandleTypeDef* getHandle() { return &_can.CanHandle; }
};

static HAL_CAN can(PIN_CAN0_RX, PIN_CAN0_TX);
static events::EventQueue eventQueue;
static rtos::Thread t;

static volatile bool rx_pending = false;
static hal_can_rx_callback_t CAN_Rx_Callback = NULL;

// Per-frame RX arrival timestamps, indexed by FDCAN RX FIFO0 hardware slot.
// mbed defers the actual frame read (can.read() takes a mutex) to a thread, so a
// single shared slot would let a later frame's IRQ overwrite the arrival time of
// an earlier frame (e.g. a SYNC) still waiting to be read — biasing the sync
// offset by up to the inter-frame gap whenever other traffic is on the bus.
// Keying the stamp to the hardware FIFO slot pairs every frame with its own
// EOF-referenced arrival time and can never permanently desync, because the
// association is the hardware get/put index, not a free-running counter.
static volatile uint32_t rx_ts_by_slot[FDCAN_RX_FIFO0_DEPTH] = {0};
static volatile uint8_t  rx_ts_stamped_pi = 0;  // next FIFO slot not yet stamped by the ISR
static uint32_t          rx_last_frame_ts = 0;  // arrival stamp of the most recent HAL_CAN_Receive (deferred-thread only)

static hal_can_tx_sync_cb_t  CAN_Tx_Sync_Callback = NULL;

// Failsafe ceiling for the HAL_CAN_Send_Sync spin. Only reached on a bus fault
// (bus-off / no ACK -> the frame auto-retransmits forever and the TX pending
// bit never clears); the healthy case exits in ~0.1-0.3 ms regardless of this
// value. Floor is the legit worst case = one in-flight max frame (~150 us) plus
// the SYNC's own tx (~130 us) ~= 350 us at 1 Mbps for a high-priority SYNC.
// 1 ms keeps ~3x margin above that. NOTE: frame times scale with baud — multiply
// by ~2/4/8 if you run at 500/250/125 kbps to avoid spurious timeouts.
#define SYNC_TX_WAIT_TIMEOUT_US 1000u

return_code_t HAL_CAN_Register_Callback(hal_can_rx_callback_t Callback) {
  if (Callback == NULL) {
    // LOG HAL_CAN_ERROR_NULL_POINTER;
    return RC_INVALID_ARG;
  }
  CAN_Rx_Callback = Callback;
  return RC_OK;
}

static void CAN_Deferred_Handler();
static void CAN_Rx_IRQHandler();

return_code_t HAL_CAN_Register_TX_Sync_Callback(hal_can_tx_sync_cb_t callback) {
    if (callback == NULL) {
        return RC_INVALID_ARG;
    }
    CAN_Tx_Sync_Callback = callback;
    return RC_OK;
}

return_code_t HAL_CAN_Send(can_msg_t *msg)
{

    if(msg == nullptr)
    {
        return RC_INVALID_ARG;
    }

    mbed::CANMessage m(msg->id, msg->data, msg->dlc, CANData, CANExtended);
    if (!can.write(m))
    {
        return RC_HW_FAULT;
    }
    else
    {
        return RC_OK;
    }
}

return_code_t HAL_CAN_Receive(can_msg_t *msg)
{
    if(msg == nullptr)
    {
        return RC_INVALID_ARG;
    }

    // Sample the FIFO get index *before* the read: can.read() consumes the frame
    // currently at this slot and then advances the index. Pairing the consumed
    // frame with rx_ts_by_slot[gi] keeps the arrival stamp matched to the exact
    // frame even when many frames are drained back-to-back here.
    FDCAN_HandleTypeDef *hfdcan = can.getHandle();
    uint8_t gi = (uint8_t)((hfdcan->Instance->RXF0S & FDCAN_RXF0S_F0GI_Msk) >> FDCAN_RXF0S_F0GI_Pos);

    mbed::CANMessage m;
    if (!can.read(m))
    {
        return RC_OK_NO_DATA;
    }
    else
    {
        rx_last_frame_ts = rx_ts_by_slot[gi];
        msg->id = m.id;
        msg->dlc = m.len;
        for (int i = 0; i < m.len; i++)
        {
            msg->data[i] = m.data[i];
        }
        return RC_OK;
    }
}

return_code_t HAL_CAN_Init_Hardware(uint32_t baudrate, uint32_t hw_filter_id, uint32_t hw_filter_mask, bool rx_int_enable)
{
    if (!can.frequency(baudrate))
    {
        return RC_HW_FAULT;
    }

    if (!can.filter(hw_filter_id, hw_filter_mask, CANExtended))
    {
        return RC_HW_FAULT;
    }

    // Increase the FDCAN RX FIFO0 depth beyond the mbed default of 8.
    // Must stop peripheral, reconfigure, re-init RAM layout, then restart.
    {
        FDCAN_HandleTypeDef *hfdcan = can.getHandle();

        if (HAL_FDCAN_Stop(hfdcan) != HAL_OK)
        {
            return RC_HW_FAULT;
        }

        hfdcan->Init.RxFifo0ElmtsNbr = FDCAN_RX_FIFO0_DEPTH;

        if (HAL_FDCAN_Init(hfdcan) != HAL_OK)
        {
            return RC_HW_FAULT;
        }

        // Re-apply global filter after re-init (defaults reject all)
        if (HAL_FDCAN_ConfigGlobalFilter(hfdcan,
                FDCAN_ACCEPT_IN_RX_FIFO0, FDCAN_ACCEPT_IN_RX_FIFO0,
                FDCAN_FILTER_REMOTE, FDCAN_FILTER_REMOTE) != HAL_OK)
        {
            return RC_HW_FAULT;
        }

        if (HAL_FDCAN_Start(hfdcan) != HAL_OK)
        {
            return RC_HW_FAULT;
        }
    }

    // Re-apply mbed-level filter after FDCAN re-init
    if (!can.filter(hw_filter_id, hw_filter_mask, CANExtended))
    {
        return RC_HW_FAULT;
    }

    if (rx_int_enable)
    {
        if(CAN_Rx_Callback == NULL) {
          // LOG HAL_CAN_ERROR_NO_CALLBACK_REGISTERED;
          return RC_INVALID_ARG;
        }

        can.attach(&CAN_Rx_IRQHandler, mbed::CAN::RxIrq);

        if (!t.start(callback(&eventQueue, &events::EventQueue::dispatch_forever)))
        {
            return RC_HW_FAULT;
        }
    }

    return RC_OK;
}

// Internal deferred handler that wraps the user callback.
// Drains all available frames, then re-checks in case new frames
// arrived during processing (closing the ISR/drain race window).
static void CAN_Deferred_Handler()
{
    do {
        rx_pending = false;
        if (CAN_Rx_Callback != NULL) {
            CAN_Rx_Callback();
        }
    } while (rx_pending);
}

// CAN RX ISR: cannot call can.read() directly due to mbed mutex.
// Defers work to the event queue. The rx_pending flag coalesces
// multiple IRQs into a single dispatch, preventing queue overflow.
static void CAN_Rx_IRQHandler()
{
    // Stamp arrival at interrupt time (close to EOF — same reference point as the
    // controller's TX timestamp). One new-message IRQ normally maps to one frame,
    // so we stamp every FIFO slot that filled since the previous IRQ. If two frames
    // ever coalesce into a single IRQ, the older slot gets this (marginally later)
    // time — bounded by the inter-frame gap, not the whole RX-batch span, and
    // self-correcting because the slots are hardware-indexed.
    uint32_t now = (uint32_t)Get_Micros();
    FDCAN_HandleTypeDef *hfdcan = can.getHandle();
    uint8_t pi = (uint8_t)((hfdcan->Instance->RXF0S & FDCAN_RXF0S_F0PI_Msk) >> FDCAN_RXF0S_F0PI_Pos);
    while (rx_ts_stamped_pi != pi) {
        rx_ts_by_slot[rx_ts_stamped_pi] = now;
        rx_ts_stamped_pi = (uint8_t)((rx_ts_stamped_pi + 1u) % FDCAN_RX_FIFO0_DEPTH);
    }

    bool was_pending = rx_pending;
    rx_pending = true;
    if (!was_pending) {
        eventQueue.call(CAN_Deferred_Handler);
    }
}

uint32_t HAL_CAN_Get_RX_Timestamp(void)
{
    // Arrival stamp of the frame returned by the most recent HAL_CAN_Receive().
    // The PAL calls this immediately after receiving a SYNC frame, before the next
    // HAL_CAN_Receive(), so the value still belongs to that SYNC.
    return rx_last_frame_ts;
}

// Adds an additional FDCAN acceptance filter entry.
// mbed CAN::filter() with handle=0 allocates the next available filter slot.
return_code_t HAL_CAN_Add_Filter(uint32_t filter_id, uint32_t filter_mask)
{
    if (!can.filter(filter_id, filter_mask, CANExtended))
    {
        return RC_HW_FAULT;
    }
    return RC_OK;
}

// Send a sync-tagged frame and timestamp it at the moment it actually leaves
// the controller (end-of-frame), not before queuing.
//
// The frame is pushed into the TX FIFO/queue, then we identify the specific TX
// buffer it occupies and poll that buffer's request-pending bit. Hardware clears
// the bit once the frame has been transmitted and ACKed, so reading Get_Micros()
// the instant it clears yields an EOF-referenced timestamp — the same reference
// point the responder's RX ISR captures on arrival. Residual sync error is then
// just bus propagation + RX-ISR latency (single-digit us) instead of the previous
// ~130 us pre-send airtime bias.
//
// This deliberately uses polling, not the FDCAN TX interrupt: mbed only services
// the IT0 line, so enabling an IT1-routed TX-event/complete interrupt jumps to an
// uninstalled vector (HardFault). The poll is bounded (~one frame time, once per
// sync period) with a timeout so a bus-off / un-ACKed frame cannot lock the loop.
//
// The callback is still invoked synchronously here; PAL's follow_up_pending path
// is unchanged and dispatches the FOLLOW_UP on the next Network_Poll.
return_code_t HAL_CAN_Send_Sync(can_msg_t *msg)
{
    if (msg == nullptr)
    {
        return RC_INVALID_ARG;
    }

    static const uint32_t dlc_map[9] = {
        FDCAN_DLC_BYTES_0, FDCAN_DLC_BYTES_1, FDCAN_DLC_BYTES_2, FDCAN_DLC_BYTES_3,
        FDCAN_DLC_BYTES_4, FDCAN_DLC_BYTES_5, FDCAN_DLC_BYTES_6, FDCAN_DLC_BYTES_7,
        FDCAN_DLC_BYTES_8
    };
    uint8_t dlc = (msg->dlc > 8u) ? 8u : msg->dlc;

    FDCAN_TxHeaderTypeDef txHeader = {};
    txHeader.Identifier          = msg->id;
    txHeader.IdType              = FDCAN_EXTENDED_ID;
    txHeader.TxFrameType         = FDCAN_DATA_FRAME;
    txHeader.DataLength          = dlc_map[dlc];
    txHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    txHeader.BitRateSwitch       = FDCAN_BRS_OFF;
    txHeader.FDFormat            = FDCAN_CLASSIC_CAN;
    txHeader.TxEventFifoControl  = FDCAN_NO_TX_EVENTS;
    txHeader.MessageMarker       = 0;

    FDCAN_HandleTypeDef *hfdcan = can.getHandle();

    // Queue the frame and capture which TX buffer it landed in. Both steps must
    // be atomic w.r.t. any other sender (e.g. the RX-deferred thread issuing a
    // response), otherwise GetLatest could report a different frame's buffer.
    CRITICAL_SECTION_ENTER();
    if (HAL_FDCAN_AddMessageToTxFifoQ(hfdcan, &txHeader, msg->data) != HAL_OK)
    {
        CRITICAL_SECTION_EXIT();
        return RC_HW_FAULT;
    }
    uint32_t tx_buffer = HAL_FDCAN_GetLatestTxFifoQRequestBuffer(hfdcan);
    CRITICAL_SECTION_EXIT();

    // Wait until this specific frame has left the controller (request-pending
    // bit clears at end-of-frame). Bounded by SYNC_TX_WAIT_TIMEOUT_US.
    uint32_t t_start = (uint32_t)Get_Micros();
    while (HAL_FDCAN_IsTxBufferMessagePending(hfdcan, tx_buffer))
    {
        if ((uint32_t)((uint32_t)Get_Micros() - t_start) > SYNC_TX_WAIT_TIMEOUT_US)
        {
            // Frame never made it onto the wire (bus-off / no ACK). Skip the
            // FOLLOW_UP for this cycle; the responder keeps its last offset until
            // SYNC_VALIDITY_US expires, then reports RC_OK_NO_DATA.
            return RC_HW_FAULT;
        }
    }

    // EOF-referenced transmit timestamp, same clock domain as the offset math.
    uint32_t t_us = (uint32_t)Get_Micros();

    if (CAN_Tx_Sync_Callback != NULL)
    {
        CAN_Tx_Sync_Callback(t_us);
    }

    return RC_OK;
}

#endif // __TARGET_PLATFORM_PORTENTA_H7__