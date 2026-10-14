#include "CONFIG.h"
#include "battservice.h"
#include "devinfoservice.h"
#include "hiddev.h"
#include "hidkbdservice.h"
#include "keyboard_app.h"
#include "control.h"

#define PA_DIR (*(volatile uint32_t *)0x400010a0u)
#define PA_PIN (*(volatile uint32_t *)0x400010a4u)
#define PA_PULLUP (*(volatile uint32_t *)0x400010b0u)
#define PA_PULLDOWN (*(volatile uint32_t *)0x400010b4u)
#define PB_DIR (*(volatile uint32_t *)0x400010c0u)
#define PB_PIN (*(volatile uint32_t *)0x400010c4u)
#define PB_PULLUP (*(volatile uint32_t *)0x400010d0u)

#define SCAN_INPUTS_EVT 0x0008
#define LED_OFF_EVT 0x0020
#define UPDATE_PARAMS_EVT 0x0010
#define SCAN_PERIOD_MS 10
#define DEBOUNCE_SAMPLES 2

#define HID_ENTER 0x28
#define HID_ESCAPE 0x29
#define HID_F24 0x73

#define PA_BUTTONS ((1u << 10) | (1u << 11) | (1u << 14))
#define JOYSTICK_PINS ((1u << 12) | (1u << 13))
#define PB_BUTTONS ((1u << 12) | (1u << 13) | (1u << 14) | \
                    (1u << 15) | (1u << 22))

typedef enum
{
    CONTROL_A,
    CONTROL_B,
    CONTROL_X,
    CONTROL_Y,
    CONTROL_MENU,
    CONTROL_OK,
    CONTROL_POWER,
    CONTROL_JOY_LEFT,
    CONTROL_JOY_RIGHT,
    CONTROL_JOY_DOWN,
    CONTROL_JOY_UP,
    CONTROL_COUNT
} Control;

typedef struct
{
    uint8_t modifiers;
    uint8_t keys[2];
} Binding;

#define NO_BINDING {0, {0, 0}}
#define KEY_BINDING(key) {0, {key, 0}}

/* Edit this table to assign the remaining controls or add shortcuts later. */
static const Binding bindings[CONTROL_COUNT] = {
    [CONTROL_A] = NO_BINDING,
    [CONTROL_B] = NO_BINDING,
    [CONTROL_X] = NO_BINDING,
    [CONTROL_Y] = KEY_BINDING(HID_ENTER),
    [CONTROL_MENU] = NO_BINDING,
    [CONTROL_OK] = NO_BINDING,
    [CONTROL_POWER] = NO_BINDING,
    [CONTROL_JOY_LEFT] = NO_BINDING,
    [CONTROL_JOY_RIGHT] = NO_BINDING,
    [CONTROL_JOY_DOWN] = KEY_BINDING(HID_F24),
    [CONTROL_JOY_UP] = KEY_BINDING(HID_ESCAPE),
};

static uint8_t task_id = INVALID_TASK_ID;
static uint16_t connection_handle = GAP_CONNHANDLE_INIT;
static uint8_t reports_enabled;
static uint16_t joystick_center_horizontal;
static uint16_t joystick_center_vertical;
static int8_t horizontal_direction;
static int8_t vertical_direction;
static uint32_t candidate_controls;
static uint32_t stable_controls;
static uint8_t candidate_samples;
static uint8_t last_report[8];
static uint16_t adc_horizontal, adc_vertical;

static uint8_t scan_response[] = {
    0x13, GAP_ADTYPE_LOCAL_NAME_COMPLETE,
    'M','O','C','U','T','E','-','0','5','2','F',' ','C','u','s','t','o','m',
};

_Static_assert(sizeof(scan_response) <= 31, "BLE scan-response exceeds 31 bytes");

static uint8_t advertising_data[] = {
    0x02, GAP_ADTYPE_FLAGS,
    GAP_ADTYPE_FLAGS_GENERAL | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,
    0x03, GAP_ADTYPE_APPEARANCE,
    LO_UINT16(GAP_APPEARE_HID_KEYBOARD),
    HI_UINT16(GAP_APPEARE_HID_KEYBOARD),
};

static const uint8_t device_name[GAP_DEVICE_NAME_LEN] = "MOCUTE-052F Custom";

static hidDevCfg_t hid_config = {
    60000,
    HID_FEATURE_FLAGS,
};

static uint16_t read_adc(uint8_t channel)
{
    uint32_t sum = 0;
    ADC_ChannelCfg(channel);
    (void)ADC_ExcutSingleConver();
    for(uint8_t i = 0; i < 4; ++i)
        sum += ADC_ExcutSingleConver();
    return (uint16_t)(sum / 4);
}

static int8_t update_axis(int8_t direction, int32_t delta)
{
    const int32_t press_threshold = 450;
    const int32_t release_threshold = 260;

    if(direction < 0 && delta < -release_threshold)
        return -1;
    if(direction > 0 && delta > release_threshold)
        return 1;
    if(delta < -press_threshold)
        return -1;
    if(delta > press_threshold)
        return 1;
    return 0;
}

static uint32_t read_controls(void)
{
    uint32_t state = 0;

    if(!(PB_PIN & (1u << 22))) state |= 1u << CONTROL_A;
    if(!(PB_PIN & (1u << 12))) state |= 1u << CONTROL_B;
    if(!(PB_PIN & (1u << 13))) state |= 1u << CONTROL_X;
    if(!(PB_PIN & (1u << 14))) state |= 1u << CONTROL_Y;
    if(!(PB_PIN & (1u << 15))) state |= 1u << CONTROL_MENU;
    if(!(PA_PIN & (1u << 10))) state |= 1u << CONTROL_OK;
    if(PA_PIN & (1u << 14)) state |= 1u << CONTROL_POWER;

    horizontal_direction = update_axis(
        horizontal_direction,
        (int32_t)(adc_horizontal=read_adc(CH_EXTIN_2)) - joystick_center_horizontal);
    vertical_direction = update_axis(
        vertical_direction,
        (int32_t)(adc_vertical=read_adc(CH_EXTIN_3)) - joystick_center_vertical);

    if(horizontal_direction < 0) state |= 1u << CONTROL_JOY_LEFT;
    if(horizontal_direction > 0) state |= 1u << CONTROL_JOY_RIGHT;
    if(vertical_direction < 0) state |= 1u << CONTROL_JOY_DOWN;
    if(vertical_direction > 0) state |= 1u << CONTROL_JOY_UP;
    return state;
}

static void make_report(uint32_t controls, uint8_t report[8])
{
    uint8_t next_key = 2;
    for(uint8_t i = 0; i < 8; ++i)
        report[i] = 0;

    for(uint8_t control = 0; control < CONTROL_COUNT; ++control)
    {
        if(!(controls & (1u << control)))
            continue;

        report[0] |= bindings[control].modifiers;
        for(uint8_t key = 0; key < 2 && next_key < 8; ++key)
        {
            if(bindings[control].keys[key])
                report[next_key++] = bindings[control].keys[key];
        }
    }
}

static uint8_t reports_equal(const uint8_t a[8], const uint8_t b[8])
{
    for(uint8_t i = 0; i < 8; ++i)
        if(a[i] != b[i])
            return FALSE;
    return TRUE;
}

static void send_current_report(void)
{
    uint8_t report[8];
    make_report(stable_controls, report);

    if(!reports_enabled || reports_equal(report, last_report))
        return;

    uint8_t status=HidDev_Report(HID_RPT_ID_KEY_IN, HID_REPORT_TYPE_INPUT,
                               sizeof(report),report);
    Control_Report(status,report);
    if(status == SUCCESS)
    {
        for(uint8_t i = 0; i < 8; ++i)
            last_report[i] = report[i];
    }
}

static void scan_inputs(void)
{
    uint32_t current = read_controls();
    if(current == candidate_controls)
    {
        if(candidate_samples < DEBOUNCE_SAMPLES)
            ++candidate_samples;
    }
    else
    {
        candidate_controls = current;
        candidate_samples = 1;
    }

    if(candidate_samples >= DEBOUNCE_SAMPLES && stable_controls != candidate_controls)
    {
        if(candidate_controls & ~stable_controls) {
            R32_PB_OUT |= 1u << 7;
            tmos_start_task(task_id,LED_OFF_EVT,MS1_TO_SYSTEM_TIME(70));
        }
        stable_controls = candidate_controls;
        send_current_report();
    }
}

static void state_changed(gapRole_States_t state, gapRoleEvent_t *event)
{
    Control_StateChanged(state,event);
    if(state == GAPROLE_CONNECTED)
    {
        connection_handle = ((gapEstLinkReqEvent_t *)event)->connectionHandle;
        tmos_start_task(task_id, UPDATE_PARAMS_EVT, 12800);
    }
    else if(state == GAPROLE_WAITING)
    {
        reports_enabled = FALSE;
        connection_handle = GAP_CONNHANDLE_INIT;
        uint8_t enable = TRUE;
        GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(enable), &enable);
    }
}

static uint8_t report_callback(uint8_t id, uint8_t type, uint16_t uuid,
                               uint8_t operation, uint16_t *length,
                               uint8_t *data)
{
    if(operation == HID_DEV_OPER_WRITE)
        return Hid_SetParameter(id, type, uuid, *length, data);
    if(operation == HID_DEV_OPER_READ)
        return Hid_GetParameter(id, type, uuid, length, data);
    if(operation == HID_DEV_OPER_DISABLE) reports_enabled=FALSE;
    if(operation == HID_DEV_OPER_ENABLE)
    {
        reports_enabled = TRUE;
        for(uint8_t i = 0; i < 8; ++i)
            last_report[i] = 0xFF;
        send_current_report();
    }
    return SUCCESS;
}

static void hid_event(uint8_t event)
{
    (void)event;
}

static hidDevCB_t hid_callbacks = {
    report_callback,
    hid_event,
    NULL,
    state_changed,
};

void KeyboardApp_Init(void)
{
    task_id = TMOS_ProcessEventRegister(KeyboardApp_ProcessEvent);

    PB_DIR &= ~PB_BUTTONS;
    PB_PULLUP |= PB_BUTTONS;
    PA_DIR &= ~(PA_BUTTONS | JOYSTICK_PINS);
    PA_PULLUP |= (1u << 10) | (1u << 11);
    PA_PULLUP &= ~(1u << 14);
    PA_PULLUP &= ~JOYSTICK_PINS;
    PA_PULLDOWN &= ~JOYSTICK_PINS;
    ADC_ExtSingleChSampInit(SampleFreq_3_2, ADC_PGA_0);

    joystick_center_horizontal = read_adc(CH_EXTIN_2);
    joystick_center_vertical = read_adc(CH_EXTIN_3);
    candidate_controls = stable_controls = read_controls();
    candidate_samples = DEBOUNCE_SAMPLES;

    uint8_t advertise = TRUE;
    GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(advertise), &advertise);
    GAPRole_SetParameter(GAPROLE_ADVERT_DATA,
                         sizeof(advertising_data), advertising_data);
    GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA,
                         sizeof(scan_response), scan_response);
    GGS_SetParameter(GGS_DEVICE_NAME_ATT, GAP_DEVICE_NAME_LEN,
                     (void *)device_name);

    uint32_t passcode = 0;
    uint8_t pairing = GAPBOND_PAIRING_MODE_WAIT_FOR_REQ;
    uint8_t mitm = FALSE;
    uint8_t io = GAPBOND_IO_CAP_NO_INPUT_NO_OUTPUT;
    uint8_t bonding = TRUE;
    GAPBondMgr_SetParameter(GAPBOND_PERI_DEFAULT_PASSCODE,
                            sizeof(passcode), &passcode);
    GAPBondMgr_SetParameter(GAPBOND_PERI_PAIRING_MODE,
                            sizeof(pairing), &pairing);
    GAPBondMgr_SetParameter(GAPBOND_PERI_MITM_PROTECTION,
                            sizeof(mitm), &mitm);
    GAPBondMgr_SetParameter(GAPBOND_PERI_IO_CAPABILITIES, sizeof(io), &io);
    GAPBondMgr_SetParameter(GAPBOND_PERI_BONDING_ENABLED,
                            sizeof(bonding), &bonding);

    if(Hid_AddService()) Control_Error(16);
    HidDev_Register(&hid_config, &hid_callbacks);
    tmos_set_event(task_id, SCAN_INPUTS_EVT);
}

uint16_t KeyboardApp_ProcessEvent(uint8_t ignored_task_id, uint16_t events)
{
    (void)ignored_task_id;

    if(events & SYS_EVENT_MSG)
    {
        uint8_t *message = tmos_msg_receive(task_id);
        if(message)
            tmos_msg_deallocate(message);
        return events ^ SYS_EVENT_MSG;
    }

    if(events & LED_OFF_EVT) {
        R32_PB_OUT &= ~(1u << 7);
        return events ^ LED_OFF_EVT;
    }
    if(events & SCAN_INPUTS_EVT)
    {
        scan_inputs();
        Control_Inputs(stable_controls,adc_horizontal,adc_vertical,reports_enabled);
        send_current_report(); /* Retry a report after a temporary busy return. */
        tmos_start_task(task_id, SCAN_INPUTS_EVT,
                        MS1_TO_SYSTEM_TIME(SCAN_PERIOD_MS));
        return events ^ SCAN_INPUTS_EVT;
    }

    if(events & UPDATE_PARAMS_EVT)
    {
        GAPRole_PeripheralConnParamUpdateReq(connection_handle,
                                             8, 8, 0, 500, task_id);
        return events ^ UPDATE_PARAMS_EVT;
    }

    return 0;
}
