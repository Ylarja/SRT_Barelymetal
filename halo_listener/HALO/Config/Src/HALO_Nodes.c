// Auto-generated HALO Node Implementations
// DO NOT EDIT MANUALLY

#include "../Private/HALO_Nodes.h"

halo_node_t HALO_NODE_CONTROLLER = {
  .name = "controller",
  .id = 0x00,
  .num_channels = 2,
  .num_controller_endpoints = 2,
  .num_responder_endpoints = 0,
  .channels = { &HALO_CHANNEL_CHANNEL1, &HALO_CHANNEL_CHANNEL2 }
};

halo_node_t HALO_NODE_RESPONDER1 = {
  .name = "responder1",
  .id = 0x01,
  .num_channels = 1,
  .num_controller_endpoints = 0,
  .num_responder_endpoints = 1,
  .channels = { &HALO_CHANNEL_CHANNEL1 }
};

halo_node_t HALO_NODE_RESPONDER2 = {
  .name = "responder2",
  .id = 0x02,
  .num_channels = 1,
  .num_controller_endpoints = 0,
  .num_responder_endpoints = 1,
  .channels = { &HALO_CHANNEL_CHANNEL2 }
};

halo_node_t HALO_NODE_LISTENER = {
  .name = "listener",
  .id = 0x03,
  .num_channels = 2,
  .num_controller_endpoints = 0,
  .num_responder_endpoints = 0,
  .channels = { &HALO_CHANNEL_CHANNEL1, &HALO_CHANNEL_CHANNEL2 }
};

