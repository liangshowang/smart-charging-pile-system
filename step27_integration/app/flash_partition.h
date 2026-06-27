/**
 * flash_partition.h — Flash 分区表 (统一维护)
 *
 * BL 和 APP 工程都 #include 此文件, 避免地址散落在多处。
 * 修改分区时只需改这一个文件。
 *
 * SWM320 内部 Flash: 512KB (0x00000 - 0x7FFFF)
 * 擦除扇区大小:        4KB
 *
 * 分区示意图:
 *   0x00000 ┌──────────────┐
 *           │ boot (64KB)   │  BootLoader (FreeRTOS + 4G + OTA)
 *   0x10000 ├──────────────┤
 *           │ conf (4KB)    │  boot_config_t (启动配置)
 *   0x11000 ├──────────────┤
 *           │ gap  (2KB)    │  对齐到 70KB
 *   0x11800 ├──────────────┤
 *           │ app  (186KB)  │  应用程序 (OTA 下载目标)
 *   0x40000 ├──────────────┤
 *           │ fdb  (256KB)  │  FlashDB 存储区
 *   0x7C000 │ fdb  (4KB)    │  当前 FlashDB (校准+凭据)
 *   0x7D000 └──────────────┘
 */

#ifndef __FLASH_PARTITION_H__
#define __FLASH_PARTITION_H__

#include <stdint.h>

/* ---- BootLoader 分区 ---- */
#define PART_BOOT_START      0x00000000
#define PART_BOOT_SIZE       (64 * 1024)      /*  64KB */

/* ---- 配置分区 ---- */
#define PART_CONF_START      0x00010000
#define PART_CONF_SIZE       (4 * 1024)       /*   4KB */

/* ---- APP 分区 ---- */
#define PART_APP_START       0x00011800       /*  70KB 偏移 */
#define PART_APP_SIZE        (186 * 1024)     /* 186KB */
#define PART_APP_END         (PART_APP_START + PART_APP_SIZE)  /* 0x40000 */

/* ---- FlashDB 分区 ---- */
#define PART_FDB_START       0x00040000
#define PART_FDB_SIZE        (256 * 1024)     /* 256KB */
#define FLASHDB_ADDR         0x0007C000       /* 当前 FlashDB 扇区 */

/* ---- 启动模式 ---- */
#define BOOT_CODE_MAGIC      0x000000A5
#define _APP_UPDATE          0   /* 留在 BL, 执行 OTA */
#define _APP_FAC             1   /* 跳转到 APP */

/* ================================================================
 * boot_config_t — CONF 分区存储的启动配置
 *
 * 布局 (放在 0x10000 起始的 4KB 扇区内):
 *   offset 0:  boot_code  (4B, magic = 0xA5)
 *   offset 4:  boot_app   (4B, 0=OTA更新, 1=启动APP)
 *   offset 8:  ota_req    (4B, 保留)
 *   offset 12: net_mode   (4B, 1=Air724, 4=Air780)
 *
 * BL 通过裸指针直接读取, 无需 FlashDB。
 * APP 通过 boot_config_write() 写入。
 * ================================================================ */
typedef struct {
    uint32_t boot_code;   /* magic: 必须 == BOOT_CODE_MAGIC */
    uint32_t boot_app;    /* _APP_UPDATE(0) or _APP_FAC(1) */
    uint32_t ota_req;     /* OTA 请求标志 */
    uint32_t net_mode;    /* 网络模块类型 */
} boot_config_t;

/* 直接内存映射访问宏 (CONF 扇区在内部 Flash, 可直接读) */
#define BOOT_CFG_PTR  ((volatile boot_config_t *)PART_CONF_START)

#endif /* __FLASH_PARTITION_H__ */
