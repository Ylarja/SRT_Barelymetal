// Auto-generated HALO Channel Implementations
// DO NOT EDIT MANUALLY

#include "../Private/HALO_Channels.h"

halo_channel_t HALO_CHANNEL_CHANNEL1 = {
  .name = "channel1",
  .global_id = 0x0020,
  .local_id = 0,
  .controller = &HALO_NODE_CONTROLLER,
  .responder = &HALO_NODE_RESPONDER1,
  .lane0_message = &TEST_MSG_DESCRIPTOR,
  .lane0_request_message = &HALO_REQUEST_MESSAGE_DESCRIPTOR,
  .lane0_rx_timeout = 10000,
  .lane1_message = &TEST_MSG_DESCRIPTOR
};

halo_channel_t HALO_CHANNEL_CHANNEL2 = {
  .name = "channel2",
  .global_id = 0x0040,
  .local_id = 0,
  .controller = &HALO_NODE_CONTROLLER,
  .responder = &HALO_NODE_RESPONDER2,
  .lane0_message = &TEST_MSG_DESCRIPTOR,
  .lane0_request_message = &HALO_REQUEST_MESSAGE_DESCRIPTOR,
  .lane0_rx_timeout = 10000,
  .lane1_message = &TEST_MSG_DESCRIPTOR
};

