

#include "../../Core_Config.h"
#ifdef __TARGET_DEVICE_ADAFRUIT_CAN_BUS_FEATHER__

#include "../Include/CAN.h"

#include <Adafruit_MCP2515.h>
#include <SPI.h>

#define PIN_CAN_CS (19u)
#define PIN_CAN_INTERRUPT (22u)
#define CS_PIN PIN_CAN_CS

// MCP2515 register and bit definitions
#define MCP2515_REG_RXB0CTRL   0x60
#define MCP2515_BIT_BUKT       0x04
#define MCP2515_SPI_BIT_MODIFY 0x05
#define MCP2515_SPI_WRITE      0x02
#define MCP2515_REG_CANINTE    0x2B
#define MCP2515_REG_CANINTF    0x2C
#define MCP2515_REG_EFLG       0x2D
#define MCP2515_REG_CANCTRL    0x0F
#define MCP2515_REG_RXF1SIDH   0x04  // RXF1: filter 1 for RXB0
#define MCP2515_FLAG_MERRF     0x80
#define MCP2515_FLAG_WAKIF     0x40
#define MCP2515_FLAG_ERRIF     0x20
#define MCP2515_FLAG_TX2IF     0x10  // CANINTF bit 4: TXB2 transmit complete (NB: 0x04 is TX0IF)
#define MCP2515_FLAG_TX2IE     0x10  // CANINTE bit 4: enable TXB2 transmit-complete interrupt
#define MCP2515_FLAG_RX1OVR    0x80
#define MCP2515_FLAG_RX0OVR    0x40

// TXB2 register addresses (dedicated sync TX buffer)
#define MCP2515_REG_TXB2CTRL   0x50
#define MCP2515_REG_TXB2SIDH   0x51
#define MCP2515_REG_TXB2DLC    0x55
#define MCP2515_REG_TXB2D0     0x56
#define MCP2515_BIT_TXREQ      0x08  // TXBnCTRL.TXREQ: request transmission
#define MCP2515_BIT_TXP_HIGH   0x03  // TXBnCTRL.TXP: highest priority
#define MCP2515_SPI_READ       0x03

static Adafruit_MCP2515 mcp(CS_PIN);

// Enable RXB0→RXB1 rollover via direct SPI bit-modify command.
// Must be called after mcp.begin() and mcp.filterExtended() as both
// overwrite RXB0CTRL without setting the BUKT bit.
static void MCP2515_Enable_Rollover() {
  SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(MCP2515_SPI_BIT_MODIFY);
  SPI.transfer(MCP2515_REG_RXB0CTRL);
  SPI.transfer(MCP2515_BIT_BUKT);  // mask: only modify BUKT bit
  SPI.transfer(MCP2515_BIT_BUKT);  // value: set BUKT = 1
  digitalWrite(CS_PIN, HIGH);
  SPI.endTransaction();
}

// Enable the TXB2 transmit-complete interrupt (TX2IE) via SPI bit-modify.
// The Adafruit library's begin() only enables RX0IE/RX1IE, so without this the
// MCP2515 sets TX2IF when a sync frame finishes transmitting but never drives
// the INT pin — the ISR would then never run at the right moment to timestamp it.
static void MCP2515_Enable_TX2_Interrupt() {
  SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(MCP2515_SPI_BIT_MODIFY);
  SPI.transfer(MCP2515_REG_CANINTE);
  SPI.transfer(MCP2515_FLAG_TX2IE);  // mask: only TX2IE
  SPI.transfer(MCP2515_FLAG_TX2IE);  // value: set TX2IE = 1
  digitalWrite(CS_PIN, HIGH);
  SPI.endTransaction();
}

// Clear MCP2515 error and overflow flags via SPI bit-modify.
// This prevents the INT pin from being held permanently LOW due to
// ERRIF/MERRF flags that parsePacket() does not clear, which would
// block all further FALLING-edge interrupts and freeze CAN reception.
static void MCP2515_Clear_Errors() {
  // Clear CANINTF error flags (MERRF, WAKIF, ERRIF)
  SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(MCP2515_SPI_BIT_MODIFY);
  SPI.transfer(MCP2515_REG_CANINTF);
  SPI.transfer(MCP2515_FLAG_MERRF | MCP2515_FLAG_WAKIF | MCP2515_FLAG_ERRIF);
  SPI.transfer(0x00);
  digitalWrite(CS_PIN, HIGH);
  SPI.endTransaction();

  // Clear EFLG RX overflow flags (RX0OVR, RX1OVR) so ERRIF is not
  // immediately re-asserted by the MCP2515.
  SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(MCP2515_SPI_BIT_MODIFY);
  SPI.transfer(MCP2515_REG_EFLG);
  SPI.transfer(MCP2515_FLAG_RX0OVR | MCP2515_FLAG_RX1OVR);
  SPI.transfer(0x00);
  digitalWrite(CS_PIN, HIGH);
  SPI.endTransaction();
}

// Disabling interrupts during send is necessary to prevent
// SPI deadlock when CAN RX interrupt occurs during TX SPI transaction.
return_code_t HAL_CAN_Send(can_msg_t *msg) {

  if (msg == NULL) {
    // LOG HAL_CAN_ERROR_NULL_POINTER;
    return RC_INVALID_ARG;
  }

  noInterrupts();
  if (!mcp.beginExtendedPacket(msg->id)) {
    // LOG HAL_CAN_ERROR_SEND_FAILURE;
    interrupts();
    return RC_HW_FAULT;
  }

  for (uint8_t i = 0; i < msg->dlc; i++) {
    mcp.write(msg->data[i]);
  }

  if (!mcp.endPacket()) {
    // LOG HAL_CAN_ERROR_SEND_FAILURE;
    interrupts();
    return RC_HW_FAULT;
  }

  interrupts();
  return RC_OK;
}

// Max messages drained per ISR invocation. The MCP2515 has 2 RX buffers;
// capping at 2 keeps the ISR short so USB/Serial interrupts are not starved
// under sustained back-to-back CAN traffic.
#define CAN_RX_DRAIN_LIMIT 2
static uint8_t rx_drain_count = 0;

return_code_t HAL_CAN_Receive(can_msg_t *msg) {

  if (msg == NULL) {
    // LOG HAL_CAN_ERROR_NULL_POINTER;
    return RC_INVALID_ARG;
  }

  // Force the caller's while-loop to exit after draining
  // CAN_RX_DRAIN_LIMIT messages so the ISR can return.
  // The LOW-level interrupt will re-fire immediately if
  // INT is still asserted, giving USB a chance between passes.
  if (rx_drain_count >= CAN_RX_DRAIN_LIMIT) {
    rx_drain_count = 0;
    MCP2515_Clear_Errors();
    return RC_OK_NO_DATA;
  }

  if (mcp.parsePacket() && mcp.packetExtended()) {

    msg->id = mcp.packetId();
    msg->dlc = mcp.packetDlc();
    if (msg->dlc > CAN_MAX_DLC) {
      msg->dlc = CAN_MAX_DLC;
    }
    for (uint8_t i = 0; i < msg->dlc; i++) {
      msg->data[i] = mcp.read();
    }
    rx_drain_count++;
  } else {
    rx_drain_count = 0;
    MCP2515_Clear_Errors();
    return RC_OK_NO_DATA;
  }

  return RC_OK;
}

static hal_can_rx_callback_t  CAN_Rx_Callback      = NULL;
static hal_can_tx_sync_cb_t   CAN_Tx_Sync_Callback = NULL;

// Local timestamp captured at RX ISR entry (before the SPI frame read), read
// back by the PAL via HAL_CAN_Get_RX_Timestamp() to stamp sync-frame arrival
// at interrupt time rather than after the ~tens-of-us SPI readback.
static volatile uint32_t rx_isr_timestamp_us = 0;

// Bounded ISR: handles TX2 complete (sync) then RX.
// TX2IF is checked first to capture the timestamp as early as possible.
// SPI re-entrancy: Arduino ISRs execute with global interrupts disabled
// (sei() is never called implicitly), so all SPI transactions in this ISR
// are serialised by hardware. No additional guards are needed.
static void CAN_Rx_Bounded_ISR() {

  // Capture a timestamp at ISR entry, before any SPI latency, in case this is
  // the TXB2 (sync) transmit-complete interrupt. It is cheap (a ticker read)
  // and simply discarded when TX2IF turns out clear. Capturing here instead of
  // after the CANINTF read removes ~one SPI transaction (~5 us) of bias from
  // the sync timestamp.
  uint32_t t_entry = (uint32_t)Get_Micros();

  // Publish the entry stamp as the RX arrival time for any frame read in this
  // ISR pass (used by the PAL for sync-frame RX timestamping).
  rx_isr_timestamp_us = t_entry;

  // Read CANINTF to check for TXB2 transmit complete flag
  uint8_t canintf = 0;
  SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(MCP2515_SPI_READ);
  SPI.transfer(MCP2515_REG_CANINTF);
  canintf = SPI.transfer(0x00);
  digitalWrite(CS_PIN, HIGH);
  SPI.endTransaction();

  if (canintf & MCP2515_FLAG_TX2IF) {
    // Clear the flag, then invoke the callback with the entry timestamp.
    SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
    digitalWrite(CS_PIN, LOW);
    SPI.transfer(MCP2515_SPI_BIT_MODIFY);
    SPI.transfer(MCP2515_REG_CANINTF);
    SPI.transfer(MCP2515_FLAG_TX2IF); // mask
    SPI.transfer(0x00);               // clear
    digitalWrite(CS_PIN, HIGH);
    SPI.endTransaction();
    if (CAN_Tx_Sync_Callback != NULL) {
      CAN_Tx_Sync_Callback(t_entry);
    }
  }

  if (CAN_Rx_Callback != NULL) {
    CAN_Rx_Callback();
  }
}

return_code_t HAL_CAN_Register_Callback(hal_can_rx_callback_t Callback) {
  if (Callback == NULL) {
    return RC_INVALID_ARG;
  }
  CAN_Rx_Callback = Callback;
  return RC_OK;
}

return_code_t HAL_CAN_Register_TX_Sync_Callback(hal_can_tx_sync_cb_t callback) {
  if (callback == NULL) {
    return RC_INVALID_ARG;
  }
  CAN_Tx_Sync_Callback = callback;
  return RC_OK;
}

uint32_t HAL_CAN_Get_RX_Timestamp(void) {
  return rx_isr_timestamp_us;
}

return_code_t HAL_CAN_Init_Hardware(uint32_t baudrate, uint32_t hw_filter_id, uint32_t hw_filter_mask, bool rx_int_enable) {

  if(baudrate != 1000000U && baudrate != 500000U && baudrate != 250000U && baudrate != 125000U) {
    // LOG HAL_CAN_ERROR_INVALID_BAUDRATE;
    return RC_INVALID_ARG;
  }

  if (!mcp.begin(baudrate)) {
    // LOG HAL_CAN_ERROR_HW_INIT_FAILURE;
    return RC_HW_FAULT;
  }

  if (rx_int_enable) {
    pinMode(PIN_CAN_INTERRUPT, INPUT_PULLUP);
    if(CAN_Rx_Callback == NULL) {
      // LOG HAL_CAN_ERROR_NO_CALLBACK_REGISTERED;
      return RC_INVALID_ARG;
    }
    attachInterrupt(digitalPinToInterrupt(PIN_CAN_INTERRUPT), CAN_Rx_Bounded_ISR, LOW);

    // Enable TX2IE so sync-frame (TXB2) transmit-complete drives INT and the ISR
    // can timestamp it. Harmless on responder nodes, which never use TXB2.
    MCP2515_Enable_TX2_Interrupt();
  }

  if (!mcp.filterExtended((long)hw_filter_id, (long)hw_filter_mask)) {
    // LOG HAL_CAN_ERROR_HW_FILTER_INIT_FAILURE;
    return RC_HW_FAULT;
  }

  // Enable RXB0→RXB1 rollover so both RX buffers are used.
  // Without this, frames that arrive while RXB0 is being read are dropped
  // instead of being stored in RXB1.
  MCP2515_Enable_Rollover();

  return RC_OK;
}

// Write a 29-bit extended filter ID into a MCP2515 filter register block.
// Must be called in configuration mode. sidh_reg is the SIDH address of the filter.
static void MCP2515_Write_Extended_Filter(uint8_t sidh_reg, uint32_t id29) {
  uint8_t sidh =  (id29 >> 21) & 0xFF;
  uint8_t sidl = (((id29 >> 18) & 0x07) << 5) | 0x08 | ((id29 >> 16) & 0x03); // EXIDE=1
  uint8_t eid8 =  (id29 >>  8) & 0xFF;
  uint8_t eid0 =   id29        & 0xFF;

  SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(MCP2515_SPI_WRITE);
  SPI.transfer(sidh_reg);
  SPI.transfer(sidh);
  SPI.transfer(sidl);
  SPI.transfer(eid8);
  SPI.transfer(eid0);
  digitalWrite(CS_PIN, HIGH);
  SPI.endTransaction();
}

static bool sync_filter_installed = false;

// Send a CAN frame via TXB2, dedicated to sync transmissions.
// TXB2 is not used by the Adafruit_MCP2515 library (which uses TXB0/TXB1),
// so it is safe to reserve it here. Interrupts are disabled during the SPI
// transaction, consistent with HAL_CAN_Send.
return_code_t HAL_CAN_Send_Sync(can_msg_t *msg) {

  if (msg == NULL) {
    return RC_INVALID_ARG;
  }

  // Pack 29-bit extended ID into MCP2515 SIDH/SIDL/EID8/EID0 format
  uint32_t id = msg->id;
  uint8_t sidh =  (id >> 21) & 0xFF;
  uint8_t sidl = (((id >> 18) & 0x07) << 5) | 0x08 | ((id >> 16) & 0x03); // EXIDE=1
  uint8_t eid8 =  (id >>  8) & 0xFF;
  uint8_t eid0 =   id        & 0xFF;
  uint8_t dlc  = (msg->dlc > CAN_MAX_DLC) ? CAN_MAX_DLC : msg->dlc;

  noInterrupts();

  // Clear any stale TX2IF before requesting transmission. INT is level (LOW)
  // triggered, so a leftover TX2IF would re-enter the ISR immediately and
  // timestamp the wrong instant. After this, the next TX2IF assertion comes
  // solely from this frame's transmit-complete.
  SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(MCP2515_SPI_BIT_MODIFY);
  SPI.transfer(MCP2515_REG_CANINTF);
  SPI.transfer(MCP2515_FLAG_TX2IF); // mask
  SPI.transfer(0x00);               // clear
  digitalWrite(CS_PIN, HIGH);
  SPI.endTransaction();

  // Write ID, DLC, and data fields into TXB2 via sequential SPI write
  SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(MCP2515_SPI_WRITE);
  SPI.transfer(MCP2515_REG_TXB2SIDH);
  SPI.transfer(sidh);
  SPI.transfer(sidl);
  SPI.transfer(eid8);
  SPI.transfer(eid0);
  SPI.transfer(dlc);
  for (uint8_t i = 0; i < dlc; i++) {
    SPI.transfer(msg->data[i]);
  }
  digitalWrite(CS_PIN, HIGH);
  SPI.endTransaction();

  // Set TXREQ + highest TX priority in TXB2CTRL
  SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(MCP2515_SPI_BIT_MODIFY);
  SPI.transfer(MCP2515_REG_TXB2CTRL);
  SPI.transfer(MCP2515_BIT_TXREQ | MCP2515_BIT_TXP_HIGH); // mask
  SPI.transfer(MCP2515_BIT_TXREQ | MCP2515_BIT_TXP_HIGH); // value
  digitalWrite(CS_PIN, HIGH);
  SPI.endTransaction();

  interrupts();
  return RC_OK;
}

// Add an additional acceptance filter using MCP2515's RXF1 slot (second filter for RXB0).
// The mask for RXB0 (RXM0) is already set by HAL_CAN_Init_Hardware to include the SYNC_FRAME
// bit. SYNC_FRAME is active-low: RXF0 accepts unicast data (SYNC_FRAME=1) and RXF1 accepts
// sync frames (SYNC_FRAME=0). Only one additional filter is supported on MCP2515 via this mechanism.
return_code_t HAL_CAN_Add_Filter(uint32_t filter_id, uint32_t filter_mask) {
  (void)filter_mask; // RXM0 is already configured correctly by HAL_CAN_Init_Hardware

  if (sync_filter_installed) {
    return RC_NO_RESOURCE; // MCP2515 RXF1 slot already taken
  }

  // Enter MCP2515 configuration mode to write filter registers
  SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(MCP2515_SPI_BIT_MODIFY);
  SPI.transfer(MCP2515_REG_CANCTRL);
  SPI.transfer(0xE0); // mask: REQOP[2:0]
  SPI.transfer(0x80); // value: config mode
  digitalWrite(CS_PIN, HIGH);
  SPI.endTransaction();
  delay(1);

  MCP2515_Write_Extended_Filter(MCP2515_REG_RXF1SIDH, filter_id);

  // Return to normal mode
  SPI.beginTransaction(SPISettings(10000000, MSBFIRST, SPI_MODE0));
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(MCP2515_SPI_BIT_MODIFY);
  SPI.transfer(MCP2515_REG_CANCTRL);
  SPI.transfer(0xE0);
  SPI.transfer(0x00); // value: normal mode
  digitalWrite(CS_PIN, HIGH);
  SPI.endTransaction();
  delay(1);

  sync_filter_installed = true;
  return RC_OK;
}

#endif // __TARGET_DEVICE_ADAFRUIT_CAN_BUS_FEATHER__