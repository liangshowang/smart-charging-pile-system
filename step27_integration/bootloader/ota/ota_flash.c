/**
 * ota_flash.c — OTA Flash 操作 + CRC16 校验
 *
 * 提供:
 *   - APP 分区擦除 (全擦, 47 个 4KB 扇区)
 *   - 逐块写入 (128B/块, 内部对齐到 16B FLASH_Write 粒度)
 *   - CRC16-IBM 校验 (多项式 0xA001)
 */

#include "flash_partition.h"
#include "SWM320.h"
#include "SWM320_flash.h"
#include "sysprt.h"
#include <string.h>

/* ---- 写入缓冲 (累积 128B 后一次写入, 对齐到 16B) ---- */
#define CHUNK_SIZE  128
#define WRITE_ALIGN 16

static uint8_t  g_wbuf[CHUNK_SIZE];
static uint32_t g_waddr;          /* 当前写入地址 (绝对 Flash 地址) */
static uint32_t g_total_written;  /* 已写入总字节数 */

/* ================================================================
 * ota_flash_erase_app — 擦除整个 APP 分区
 *
 * APP 分区: PART_APP_START ~ PART_APP_END (186KB)
 * 每扇区 4KB, 共 47 个扇区 (186/4 = 46.5, 向上取整 47)
 * ================================================================ */
int ota_flash_erase_app(void)
{
    uint32_t addr;
    uint32_t sector_count;
    uint32_t i;

    Flash_Param_at_xMHz(120);

    sector_count = PART_APP_SIZE / 4096;
    if (PART_APP_SIZE % 4096) sector_count++;

    sysprt->alog("[ota_flash] erasing APP partition "
                 "(%lu sectors, 0x%08X ~ 0x%08X)...\r\n",
                 sector_count, PART_APP_START, PART_APP_END);

    for (i = 0; i < sector_count; i++) {
        addr = PART_APP_START + i * 4096;
        FLASH_Erase(addr);
    }

    /* 验证擦除: 读第一个字应为 0xFFFFFFFF */
    {
        uint32_t *p = (uint32_t *)PART_APP_START;
        if (p[0] != 0xFFFFFFFF) {
            sysprt->aerr("[ota_flash] erase verify FAILED at 0x%08X: "
                         "0x%08X\r\n", PART_APP_START, p[0]);
            return -1;
        }
    }

    sysprt->alog("[ota_flash] APP partition erased OK\r\n");
    return 0;
}

/* ================================================================
 * ota_flash_init_write — 初始化写入指针
 * ================================================================ */
void ota_flash_init_write(void)
{
    g_waddr = PART_APP_START;
    g_total_written = 0;
    memset(g_wbuf, 0xFF, sizeof(g_wbuf));
}

/* ================================================================
 * ota_flash_write_chunk — 写入 128 字节到 APP 分区
 *
 * FLASH_Write 要求:
 *   - 地址 16 字节对齐
 *   - 写入长度 (count) 是 4 的倍数 (4 words = 128bit)
 *   - 128 字节 = 32 words = 8 次 128-bit 写入
 *
 * 返回: 0=成功, -1=失败
 * ================================================================ */
int ota_flash_write_chunk(const uint8_t *data, uint32_t len)
{
    uint32_t words;
    uint32_t i;

    if (data == NULL || len != CHUNK_SIZE) return -1;
    if (g_waddr + len > PART_APP_END) {
        sysprt->aerr("[ota_flash] write overflow! addr=0x%08X len=%lu\r\n",
                     g_waddr, len);
        return -1;
    }

    Flash_Param_at_xMHz(120);

    /* FLASH_Write 要求 count = 4 的倍数 (32-bit words)
     * 128 字节 = 32 words — 分 8 次写入, 每次 16 字节 (4 words) */
    words = len / 4;  /* 128/4 = 32 */

    for (i = 0; i < words; i += 4) {
        if (FLASH_Write(g_waddr + i * 4,
                        (uint32_t *)(data + i * 4), 4) != FLASH_RES_OK) {
            sysprt->aerr("[ota_flash] write FAILED at 0x%08X\r\n",
                         g_waddr + i * 4);
            return -1;
        }
    }

    g_waddr += len;
    g_total_written += len;

    return 0;
}

/* ================================================================
 * ota_flash_get_progress — 获取写入进度
 * ================================================================ */
uint32_t ota_flash_get_written(void)
{
    return g_total_written;
}

/* ================================================================
 * crc16_ibm — CRC16-IBM 校验
 *
 * 多项式: 0xA001 (reflected 0x8005)
 * 初始值: 0x0000
 * 无最终异或
 *
 * 与 V5.5 BL 和 024_BL 的实现完全一致。
 * 用于验证 OTA 下载的每个 130 字节块。
 * ================================================================ */
uint16_t crc16_ibm(const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint16_t crc = 0;
    size_t i;
    int bit;

    for (i = 0; i < size; i++) {
        crc ^= bytes[i];
        for (bit = 0; bit < 8; bit++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc = (crc >> 1);
            }
        }
    }

    return crc;
}

/* ================================================================
 * crc16_verify_chunk — 验证 130 字节块
 *
 * 块格式: [128B data] [2B CRC16 little-endian]
 *   - dbuf[128] = CRC 低字节
 *   - dbuf[129] = CRC 高字节
 *
 * 返回: 1=CRC正确, 0=CRC错误
 * ================================================================ */
int crc16_verify_chunk(const uint8_t *dbuf)
{
    uint16_t crc_calc;
    uint16_t crc_stored;

    if (dbuf == NULL) return 0;

    crc_calc = crc16_ibm(dbuf, 128);

    /* 块格式: 低字节在前 (little-endian) */
    crc_stored = (uint16_t)dbuf[128] | ((uint16_t)dbuf[129] << 8);

    return (crc_calc == crc_stored) ? 1 : 0;
}
