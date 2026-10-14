/* WCH HAL RTC adaptation: polled clock; interrupt-driven sleep omitted.
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "HAL.h"
#include "control.h"

#if MOCUTE_DIAGNOSTICS
uint16_t calibration_flash, calibration_ram;
#endif

void HAL_TimeInit(void)
{
    /* TMOS polls the RTC counter; this build does not use interrupt-driven sleep. */
    PFIC_DisableIRQ(RTC_IRQn);
    PFIC_ClearPendingIRQ(RTC_IRQn);
    R8_SAFE_ACCESS_SIG = SAFE_ACCESS_SIG1;
    R8_SAFE_ACCESS_SIG = SAFE_ACCESS_SIG2;
    R8_CK32K_CONFIG &= ~(RB_CLK_OSC32K_XT | RB_CLK_XT32K_PON);
    R8_CK32K_CONFIG |= RB_CLK_INT32K_PON;
    R8_SAFE_ACCESS_SIG = 0;
#if MOCUTE_DIAGNOSTICS
    calibration_flash = Calibration_LSI_FLASH();
    calibration_ram = Calibration_LSI_RAM();
#else
    (void)Calibration_LSI_FLASH();
    (void)Calibration_LSI_RAM();
#endif
    TMOS_Set32KTuneValue(Int32K_Tune_FLASH, Int32K_Tune_RAM);
    RTC_InitTime(2020, 1, 1, 0, 0, 0);
    TMOS_TimerInit(0);
}
