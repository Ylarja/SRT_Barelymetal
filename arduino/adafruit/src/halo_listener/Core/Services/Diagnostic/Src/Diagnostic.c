#include "../Include/Diagnostic.h"
#include "../../../Core.h"
#include "../../../Core_Config.h"


static const char *diagnostic_event_level_strings[];

static const char *diagnostic_event_strings[];

typedef struct {
  diagnostic_event_level_t level;
} diagnostic_event_table_t;


static const diagnostic_event_table_t diag_evt_table[];

typedef struct {
  uint64_t timestamp;
  diagnostic_event_level_t level;
  diagnostic_event_t event;
  diagnostic_event_param_t params[DIAG_EVENT_MAX_PARAMS];
} diagnostic_entry_t;

typedef struct {
  diagnostic_entry_t entries[DIAG_LOG_SIZE];
  uint8_t length;
  volatile uint8_t index;
} diagnostic_log_t;

_Static_assert(DIAG_LOG_SIZE <= 255, "Diagnostic log size exceeds 255 entries");


//__attribute__((noinit))
static diagnostic_log_t diag_log;

static diagnostic_event_level_t current_log_level = DIAG_LVL_INFO;

void Diagnostic_Init(diagnostic_event_level_t level) {
  if (level >= DIAG_LVL_MAX || level < DIAG_LVL_INFO) {
    level = DIAG_LVL_INFO;
  } else {
    current_log_level = level;
  }
  diag_log.index = 0;
  diag_log.length = 0;
}

void Diagnostic_Log_Event(diagnostic_event_t event) {

  if (diag_evt_table[event].level < current_log_level) {
    return; // Noop, level too low
  }

  diagnostic_entry_t *entry = &diag_log.entries[diag_log.index];
  entry->timestamp = 0; // TODO: Get current timestamp
  entry->level = diag_evt_table[event].level;
  entry->event = event;
  diag_log.index = (diag_log.index + 1) % DIAG_LOG_SIZE;
  if (diag_log.length < DIAG_LOG_SIZE) {
    diag_log.length++;
  }
}

void Diagnostic_Log_Event_Params(diagnostic_event_t event, diagnostic_event_param_t *params, uint8_t param_count) {

  if (diag_evt_table[event].level < current_log_level) {
    return; // Noop, level too low
  }

  diagnostic_entry_t *entry = &diag_log.entries[diag_log.index];
  entry->timestamp = 0; // TODO: Get current timestamp
  entry->level = diag_evt_table[event].level;
  entry->event = event;

  // Copy parameters
  for (uint8_t i = 0; i < param_count && i < DIAG_EVENT_MAX_PARAMS; i++) {
    entry->params[i] = params[i];
  }

  diag_log.index = (diag_log.index + 1) % DIAG_LOG_SIZE;
  if (diag_log.length < DIAG_LOG_SIZE) {
    diag_log.length++;
  }
}

void Diagnostic_Log_Get_Nth(uint8_t n, diagnostic_entry_t *entry) {
  if (n >= diag_log.length) {
    return; // Out of bounds
  }
  uint8_t idx = (diag_log.index + DIAG_LOG_SIZE - diag_log.length + n) % DIAG_LOG_SIZE;
  *entry = diag_log.entries[idx];
}

void Diagnostic_Log_Print_Nth(uint8_t n) {

  char entry[100];
  if (n >= diag_log.length) {
    return; // Out of bounds
  }
  uint8_t idx = (diag_log.index + DIAG_LOG_SIZE - diag_log.length + n) % DIAG_LOG_SIZE;
  diagnostic_entry_t *diag_entry = &diag_log.entries[idx];
  snprintf(entry, 100, "T=%llu, S=%s, E=%s", diag_entry->timestamp, diagnostic_event_level_strings[diag_entry->level],
           diagnostic_event_strings[diag_entry->event]);

  HAL_Virtual_Serial_Write(entry);
}

void Diagnostic_Log_Clear() {
  diag_log.length = 0;
  diag_log.index = 0;
}

uint8_t Diagnostic_Log_Get_Length() { return diag_log.length; }

void Diagnostic_Log_Print_All() {
  for (uint8_t i = 0; i < diag_log.length; i++) {
    char entry[100];
    Diagnostic_Log_Print_Nth(i);
  }
}

void Diagnostic_Log_Print_Last_N(uint8_t n) {
  if (n > diag_log.length) {
    n = diag_log.length;
  }
  for (uint8_t i = diag_log.length - n; i < diag_log.length; i++) {
    Diagnostic_Log_Print_Nth(i);
  }
}

void Diagnostic_Log_Print_Severity(diagnostic_event_level_t level) {
  for (uint8_t i = 0; i < diag_log.length; i++) {
    uint8_t idx = (diag_log.index + DIAG_LOG_SIZE - diag_log.length + i) % DIAG_LOG_SIZE;
    diagnostic_entry_t *diag_entry = &diag_log.entries[idx];
    if (diag_entry->level == level) {
      Diagnostic_Log_Print_Nth(i);
    }
  }
}

void Diagnostic_Log_Print_Event(diagnostic_event_t event) {
  for (uint8_t i = 0; i < diag_log.length; i++) {
    uint8_t idx = (diag_log.index + DIAG_LOG_SIZE - diag_log.length + i) % DIAG_LOG_SIZE;
    diagnostic_entry_t *diag_entry = &diag_log.entries[idx];
    if (diag_entry->event == event) {
      Diagnostic_Log_Print_Nth(i);
    }
  }
}

static const diagnostic_event_table_t diag_evt_table[DIAG_EVT_MAX] = {
    [DIAG_EVT_HAL_CAN_ERR_NULL_PTR] = {DIAG_LVL_ERROR},
    [DIAG_EVT_HAL_CAN_ERR_HW_INIT_FAIL] = {DIAG_LVL_FATAL},
    [DIAG_EVT_HAL_CAN_ERR_HW_FILTER_INIT_FAIL] = {DIAG_LVL_FATAL},
    [DIAG_EVT_HAL_CAN_ERR_TX_FAIL] = {DIAG_LVL_ERROR},
    [DIAG_EVT_HAL_CAN_ERR_RX_FAIL] = {DIAG_LVL_ERROR},

};

static const char *diagnostic_event_level_strings[] = {
    [DIAG_LVL_INFO] = "INFO", [DIAG_LVL_WARNING] = "WARNING", [DIAG_LVL_ERROR] = "ERROR", [DIAG_LVL_FATAL] = "FATAL"};

static const char *diagnostic_event_strings[] = {
    [DIAG_EVT_HAL_CAN_ERR_NULL_PTR] = "HAL_CAN_ERR_NULL_PTR",
    [DIAG_EVT_HAL_CAN_ERR_HW_INIT_FAIL] = "HAL_CAN_ERR_HW_INIT_FAIL",
    [DIAG_EVT_HAL_CAN_ERR_HW_FILTER_INIT_FAIL] = "HAL_CAN_ERR_HW_FILTER_INIT_FAIL",
    [DIAG_EVT_HAL_CAN_ERR_TX_FAIL] = "HAL_CAN_ERR_TX_FAIL",
    [DIAG_EVT_HAL_CAN_ERR_RX_FAIL] = "HAL_CAN_ERR_RX_FAIL",
};