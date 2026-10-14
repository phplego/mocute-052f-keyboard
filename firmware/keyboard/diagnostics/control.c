#include "HAL.h"
#include "control.h"
#define BUILD 6
#define RESET_EVT 1
#define OTA_EVT 2
#define REPORT_EVT 4
#define LED (1u << 7)
extern uint16_t calibration_flash, calibration_ram;
static uint8_t task, state, errors, last_opcode, last_status, disconnect_reason;
static uint8_t reports_enabled, report_status, last_report[8];
static uint16_t adc_x, adc_y, sent, failures;
static uint32_t inputs;

/* Private 128-bit UUIDs: 925dd001/2-8a86-4ab8-ae90-620fb46c19e4. */
static const uint8_t svc_uuid[16] = {0xe4,0x19,0x6c,0xb4,0x0f,0x62,0x90,0xae,0xb8,0x4a,0x86,0x8a,0x01,0xd0,0x5d,0x92};
static const uint8_t cmd_uuid[16] = {0xe4,0x19,0x6c,0xb4,0x0f,0x62,0x90,0xae,0xb8,0x4a,0x86,0x8a,0x02,0xd0,0x5d,0x92};
static gattAttrType_t service = {16, svc_uuid};
static uint8_t props = GATT_PROP_READ | GATT_PROP_WRITE;
static uint8_t response[20] = {BUILD};
static gattAttribute_t attrs[] = {
    {{2, primaryServiceUUID}, GATT_PERMIT_READ, 0, (uint8_t *)&service},
    {{2, characterUUID}, GATT_PERMIT_READ, 0, &props},
    {{16, cmd_uuid}, GATT_PERMIT_READ | GATT_PERMIT_WRITE, 0, response},
};
static bStatus_t read_command(uint16_t conn, gattAttribute_t *attr, uint8_t *value,
    uint16_t *length, uint16_t offset, uint16_t max, uint8_t method)
{
    (void)conn; (void)method;
    if(attr->type.len != 16) return ATT_ERR_ATTR_NOT_FOUND;
    if(offset > sizeof(response)) return ATT_ERR_INVALID_OFFSET;
    *length = MIN(max, sizeof(response)-offset);
    tmos_memcpy(value, response+offset, *length);
    return SUCCESS;
}
#if MOCUTE_SOFTWARE_OTA
#include "ota.h"
#endif

static bStatus_t write_command(uint16_t conn, gattAttribute_t *attr, uint8_t *value,
    uint16_t length, uint16_t offset, uint8_t method)
{
    (void)conn; (void)method;
    if(attr->type.len != 16) return ATT_ERR_ATTR_NOT_FOUND;
    if(offset || !length) return ATT_ERR_INVALID_VALUE_SIZE;
    uint8_t op = value[0];
    /* Read only on-chip flash; no arbitrary memory writes or jumps. */
    if(op == 3) {
        if(length != 5) return ATT_ERR_INVALID_VALUE_SIZE;
        uint32_t address = (uint32_t)value[1] | ((uint32_t)value[2]<<8) |
            ((uint32_t)value[3]<<16) | ((uint32_t)value[4]<<24);
        if(address > 0x77ff0 || (address & 15)) return ATT_ERR_INVALID_VALUE;
        for(uint8_t i=0; i<16; ++i) response[4+i] = *(volatile uint8_t *)(address+i);
    } else if(op == 1 && length == 1) {
        for(uint8_t i=0; i<4; ++i) response[4+i] = TMOS_GetSystemClock() >> (8*i);
    } else if(op == 6 && length == 1) {
        for(uint8_t i=0; i<4; ++i) response[4+i] = inputs >> (8*i);
        response[8]=adc_x; response[9]=adc_x>>8;
        response[10]=adc_y; response[11]=adc_y>>8;
        response[12]=report_status; response[13]=errors;
        response[14]=reports_enabled; response[15]=disconnect_reason;
        response[16]=calibration_flash; response[17]=calibration_flash>>8;
        response[18]=calibration_ram; response[19]=calibration_ram>>8;
    } else if(op == 8 && length == 1) {
        uint32_t ticks = TMOS_GetSystemClock();
        for(uint8_t i=0; i<4; ++i) response[4+i]=ticks>>(8*i);
        uint32_t ctl = SysTick->CTLR;
        for(uint8_t i=0; i<4; ++i) response[8+i]=ctl>>(8*i);
        response[12]=PFIC_GetStatusIRQ(SysTick_IRQn);
        response[13]=PFIC_GetStatusIRQ(RTC_IRQn);
        response[14]=R8_RTC_MODE_CTRL;
        response[15]=R8_RTC_FLAG_CTRL;
        response[16]=Int32K_Tune_FLASH; response[17]=Int32K_Tune_FLASH>>8;
        response[18]=Int32K_Tune_RAM; response[19]=Int32K_Tune_RAM>>8;
    } else if(op == 7 && length == 1) {
        tmos_memcpy(response+4, last_report, 8);
        response[12]=sent; response[13]=sent>>8;
        response[14]=failures; response[15]=failures>>8;
        response[16]=last_opcode; response[17]=last_status;
        response[18]=state; response[19]=errors;
    } else if(op == 2 && length == 2) {
        if(value[1]) R32_PB_OUT |= LED; else R32_PB_OUT &= ~LED;
    } else if(op == 4 && length == 1) {
        tmos_start_task(task, RESET_EVT, MS1_TO_SYSTEM_TIME(500));
    }
#if MOCUTE_SOFTWARE_OTA
    else if(op == 5 && length == 1) {
        if(!loader_matches()) return ATT_ERR_UNLIKELY;
        tmos_start_task(task, OTA_EVT, MS1_TO_SYSTEM_TIME(1000));
    }
#endif
    else return ATT_ERR_INVALID_VALUE;
    response[0]=BUILD; response[1]=op; response[2]=state; response[3]=0;
    return SUCCESS;
}
static gattServiceCBs_t service_callbacks = {read_command, write_command, NULL};


static uint16_t process(uint8_t id, uint16_t events) {
    if(events & SYS_EVENT_MSG) {
        uint8_t *msg=tmos_msg_receive(id);
        if(msg) tmos_msg_deallocate(msg);
        return events ^ SYS_EVENT_MSG;
    }
#if MOCUTE_SOFTWARE_OTA
    if(events & OTA_EVT) ota_enter();
#endif
    if(events & RESET_EVT) SYS_ResetExecute();
    if(events & REPORT_EVT) {
        /* Complete name and 7 diagnostic bytes: exactly 31 scan-response bytes. */
        uint8_t scan[] = {19, GAP_ADTYPE_LOCAL_NAME_COMPLETE,
            'M','O','C','U','T','E','-','0','5','2','F',' ','C','u','s','t','o','m',
            10, GAP_ADTYPE_MANUFACTURER_SPECIFIC, 0xff,0xff,
            BUILD,state,errors,report_status,reports_enabled,inputs,inputs>>8};
        _Static_assert(sizeof(scan) <= 31, "BLE scan-response exceeds 31 bytes");
        uint8_t rc=GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA,sizeof(scan),scan);
        if(rc) errors |= 8;
        return events ^ REPORT_EVT;
    }
    return 0;
}
void Control_Init(void) {
    task=TMOS_ProcessEventRegister(process);
    if(calibration_flash == 0xffff || calibration_ram == 0xffff) errors |= 1;
    if(GATTServApp_RegisterService(attrs,3,GATT_MAX_ENCRYPT_KEY_SIZE,&service_callbacks)) errors |= 2;
    tmos_start_task(task,REPORT_EVT,MS1_TO_SYSTEM_TIME(50));
}
void Control_StateChanged(gapRole_States_t next, gapRoleEvent_t *event) {
    state=next;
    if(event) {
        last_opcode=event->gap.opcode;
        last_status=event->gap.hdr.status;
        if(last_opcode==GAP_LINK_TERMINATED_EVENT) disconnect_reason=event->linkTerminate.reason;
    }
    if(next==GAPROLE_ERROR) errors |= 4;
    if(!event || event->gap.opcode!=GAP_ADV_DATA_UPDATE_DONE_EVENT)
        tmos_start_task(task,REPORT_EVT,MS1_TO_SYSTEM_TIME(100));
}
void Control_Inputs(uint32_t value,uint16_t x,uint16_t y,uint8_t enabled) {
    inputs=value; adc_x=x; adc_y=y; reports_enabled=enabled;
}
void Control_Report(uint8_t rc,const uint8_t report[8]) {
    report_status=rc;
    if(rc==SUCCESS) { ++sent; tmos_memcpy(last_report,report,8); }
    else ++failures;
}
void Control_Error(uint8_t mask) { errors |= mask; }
