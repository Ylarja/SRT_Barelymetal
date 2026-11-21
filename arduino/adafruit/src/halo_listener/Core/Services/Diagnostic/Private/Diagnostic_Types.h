#ifndef DIAGNOSTIC_TYPES_H
#define DIAGNOSTIC_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  RC_SUCCESS = 1,
  RC_FAILURE = 0

} return_code_old_t;

typedef enum {
  DIAG_EVT_HAL_CAN_ERR_NULL_PTR,
  DIAG_EVT_HAL_CAN_ERR_HW_INIT_FAIL,
  DIAG_EVT_HAL_CAN_ERR_HW_FILTER_INIT_FAIL,
  DIAG_EVT_HAL_CAN_ERR_TX_FAIL,
  DIAG_EVT_HAL_CAN_ERR_RX_FAIL,

  DIAG_EVT_MAX
} diagnostic_event_t;

typedef struct {
  char name[10];
  uint32_t value;
  uint8_t scale; // Scale factor (e.g., 0 for no scaling, 1 for x10, etc.)
} diagnostic_event_param_t;

typedef enum {
  DIAG_LVL_INFO = 0,
  DIAG_LVL_WARNING,
  DIAG_LVL_ERROR,
  DIAG_LVL_FATAL,

  DIAG_LVL_MAX
} diagnostic_event_level_t;

#ifdef __cplusplus
}
#endif

#endif // DIAGNOSTIC_TYPES_H