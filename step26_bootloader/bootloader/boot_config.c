/**
 * boot_config.c — 启动配置读写 (CONF 分区)
 *
 * BL 通过裸指针直接读, APP 通过 IAP 函数写。
 * 位于 0x10000, 不依赖 FreeRTOS 或 FlashDB。
 */

#include "flash_partition.h"
#include "SWM320.h"
#include "SWM320_flash.h"
#include <string.h>

/* ================================================================
 * boot_config_read — 读取启动配置 (内存映射, 无需初始化)
 *
 * 返回: 0=成功 (magic 匹配), -1=失败 (magic 不匹配/未初始化)
 * ================================================================ */
int boot_config_read(boot_config_t *cfg)
{
    if (cfg == NULL) return -1;

    /* 直接读内部 Flash (内存映射, 零开销) */
    cfg->boot_code = BOOT_CFG_PTR->boot_code;
    cfg->boot_app  = BOOT_CFG_PTR->boot_app;
    cfg->ota_req   = BOOT_CFG_PTR->ota_req;
    cfg->net_mode  = BOOT_CFG_PTR->net_mode;

    if (cfg->boot_code != BOOT_CODE_MAGIC) {
        return -1;
    }
    return 0;
}

/* ================================================================
 * boot_config_get_boot_app — 快速获取 boot_app (仅读一个字段)
 *
 * 用于 BL 早期判断: jump to APP or stay in BL.
 * ================================================================ */
uint32_t boot_config_get_boot_app(void)
{
    return BOOT_CFG_PTR->boot_app;
}

/* ================================================================
 * boot_config_is_valid — 检查 CONF 分区是否已初始化
 * ================================================================ */
int boot_config_is_valid(void)
{
    return (BOOT_CFG_PTR->boot_code == BOOT_CODE_MAGIC) ? 1 : 0;
}

/* ================================================================
 * boot_config_write — 写入启动配置到 CONF 分区
 *
 * 擦除 4KB 扇区, 然后写入 16 字节 (4 words, 对齐要求).
 * ================================================================ */
int boot_config_write(const boot_config_t *cfg)
{
    uint32_t buf[4];

    if (cfg == NULL) return -1;

    Flash_Param_at_xMHz(120);

    /* 构建写缓冲 (4 words = 16 bytes, FLASH_Write 最小粒度) */
    buf[0] = cfg->boot_code;
    buf[1] = cfg->boot_app;
    buf[2] = cfg->ota_req;
    buf[3] = cfg->net_mode;

    /* 擦除 → 写入 */
    FLASH_Erase(PART_CONF_START);
    if (FLASH_Write(PART_CONF_START, buf, 4) != FLASH_RES_OK) {
        return -1;
    }

    return 0;
}

/* ================================================================
 * boot_config_set_boot_app — 仅更新 boot_app 字段 (保留其他字段)
 * ================================================================ */
int boot_config_set_boot_app(uint32_t boot_app)
{
    boot_config_t cfg;

    /* 读当前值 (如果未初始化, 用默认值) */
    if (boot_config_read(&cfg) != 0) {
        cfg.boot_code = BOOT_CODE_MAGIC;
        cfg.boot_app  = boot_app;
        cfg.ota_req   = 0;
        cfg.net_mode  = 1;  /* 默认 Air724 */
    } else {
        cfg.boot_app = boot_app;
    }

    return boot_config_write(&cfg);
}
