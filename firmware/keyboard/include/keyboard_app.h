#ifndef KEYBOARD_APP_H
#define KEYBOARD_APP_H

#include <stdint.h>

void KeyboardApp_Init(void);
uint16_t KeyboardApp_ProcessEvent(uint8_t task_id, uint16_t events);

#endif
