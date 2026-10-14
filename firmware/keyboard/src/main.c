#include "CONFIG.h"
#include "HAL.h"
#include "hiddev.h"
#include "keyboard_app.h"
#include "control.h"

__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];
#if BLE_MAC
const uint8_t MacAddr[6] = MOCUTE_MAC_BYTES;
#endif

int main(void)
{
    R32_PB_OUT |= 1u << 23; /* power latch */
    R32_PB_OUT &= ~(1u << 7);
    R32_PB_DIR |= (1u << 23) | (1u << 7);
    SetSysClock(CLK_SOURCE_PLL_60MHz);
    CH57X_BLEInit();
    HAL_Init();
    GAPRole_PeripheralInit();
    HidDev_Init();
    Control_Init();
    KeyboardApp_Init();
    for(;;) TMOS_SystemProcess();
}
