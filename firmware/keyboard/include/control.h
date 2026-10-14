#ifndef MOCUTE_CONTROL_H
#define MOCUTE_CONTROL_H
#include "CONFIG.h"
#ifndef MOCUTE_DIAGNOSTICS
#define MOCUTE_DIAGNOSTICS 1
#endif
#if MOCUTE_DIAGNOSTICS
void Control_Init(void);
void Control_StateChanged(gapRole_States_t state, gapRoleEvent_t *event);
void Control_Inputs(uint32_t controls, uint16_t x, uint16_t y, uint8_t enabled);
void Control_Report(uint8_t status, const uint8_t report[8]);
void Control_Error(uint8_t mask);
#else
#define Control_Init() ((void)0)
#define Control_StateChanged(state, event) ((void)0)
#define Control_Inputs(controls, x, y, enabled) ((void)0)
#define Control_Report(status, report) ((void)0)
#define Control_Error(mask) ((void)0)
#endif
#endif
