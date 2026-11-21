#ifndef HALO_TYPES_H
#define HALO_TYPES_H

#include <stdint.h>
#include "../../../Core/DOL/Include/DOL.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef object_desc_t msg_desc_t;
typedef struct halo_channel_t halo_channel_t; // Forward declaration to break circular dependency
typedef struct halo_node_t halo_node_t; // Forward declaration to break circular dependency

struct halo_node_t {
  const char *name;
  const uint8_t id;
  const uint16_t num_channels;
  const uint8_t num_controller_endpoints; // Number of channels for which this node is the controller (used for determining controller/responder roles and corresponding message descriptors)
  const uint8_t num_responder_endpoints;  // Number of channels for which this node is the responder (used for determining controller/responder roles and corresponding message descriptors)
  const halo_channel_t* channels[]; // Array of pointers to the channels this node is connected to (size determined by num_channels)
};

struct halo_channel_t {
  const char *name;
  const uint16_t global_id;
  const uint8_t local_id;
  const halo_node_t *controller;
  const halo_node_t *responder;
  const msg_desc_t *lane0_message;
  const msg_desc_t *lane0_request_message;
  const uint64_t lane0_rx_timeout;
  const msg_desc_t *lane1_message;
};


#ifdef __cplusplus
}
#endif

#endif // HALO_TYPES_H