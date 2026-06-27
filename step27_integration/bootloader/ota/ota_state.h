/**
 * ota_state.h — OTA 状态机接口
 */
#ifndef __OTA_STATE_H__
#define __OTA_STATE_H__

#include <stdint.h>
#include "air_man.h"

void     ota_state_init(void);
int      ota_state_work(void);
uint8_t  ota_state_get_phase(void);
uint32_t ota_state_get_file_size(void);
uint32_t ota_state_get_recv_size(void);

/* 全局设备指针 (由 task_start 设置) */
extern air_dev_pt g_dev;

#endif
