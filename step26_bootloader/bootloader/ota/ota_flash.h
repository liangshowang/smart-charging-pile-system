/**
 * ota_flash.h — OTA Flash 操作 + CRC16 接口
 */
#ifndef __OTA_FLASH_H__
#define __OTA_FLASH_H__

#include <stdint.h>
#include <stddef.h>

int      ota_flash_erase_app(void);
void     ota_flash_init_write(void);
int      ota_flash_write_chunk(const uint8_t *data, uint32_t len);
uint32_t ota_flash_get_written(void);

uint16_t crc16_ibm(const void *data, size_t size);
int      crc16_verify_chunk(const uint8_t *dbuf);

#endif
