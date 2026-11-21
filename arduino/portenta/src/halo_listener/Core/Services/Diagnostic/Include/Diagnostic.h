#ifndef DIAGNOSTIC_H
#define DIAGNOSTIC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../Private/Diagnostic_Types.h"
#include <stdint.h> 
#include <stdbool.h>


void Diagnostic_Init(diagnostic_event_level_t level);

void Diagnostic_Log_Event(diagnostic_event_t event);
void Diagnostic_Log_Event_Params(diagnostic_event_t event, diagnostic_event_param_t *params, uint8_t param_count);
//void Diagnostic_Log_Get_Nth(uint8_t n, diagnostic_entry_t* entry);
void Diagnostic_Log_Print_Nth(uint8_t n);
void Diagnostic_Log_Print_All();
void Diagnostic_Log_Print_Last_N(uint8_t n);
void Diagnostic_Log_Clear();
uint8_t Diagnostic_Log_Get_Length();





#ifdef __cplusplus
}
#endif

#endif // DIAGNOSTIC_H