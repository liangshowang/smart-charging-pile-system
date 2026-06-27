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
 *           │ boot (64KB)   │  BootLoader
 *   0x10000 ├──────────────┤
 *           │ conf (4KB)    │  启动标志 + 预留
 *   0x11000 ├──────────────┤
 *           │ gap  (2KB)    │  对齐到 70KB
 *   0x11800 ├──────────────┤
 *           │ app  (186KB)  │  应用程序
 *   0x40000 ├──────────────┤
 *           │ fdb  (248KB)  │  FlashDB 存储区 (预留扩展)
 *   0x7C000 │ fdb  (4KB)    │  当前 FlashDB
 *   0x7D000 └──────────────┘
 */

#ifndef __FLASH_PARTITION_H__
#define __FLASH_PARTITION_H__

/* ---- BootLoader 分区 ---- */
#define PART_BOOT_START      0x00000000
#define PART_BOOT_SIZE       (64 * 1024)      /*  64KB */

/* ---- 配置分区 (启动标志) ---- */
#define PART_CONF_START      0x00010000
#define PART_CONF_SIZE       (4 * 1024)       /*   4KB */
#define PART_CONF_MAGIC      0x5A5A5A5A       /* APP 有效标志 */

/* ---- APP 分区 ---- */
#define PART_APP_START       0x00011800       /*  70KB 偏移 */
#define PART_APP_SIZE        (186 * 1024)     /* 186KB */

/* ---- FlashDB 分区 ---- */
#define PART_FDB_START       0x00040000
#define PART_FDB_SIZE        (256 * 1024)     /* 256KB */
#define FLASHDB_ADDR         0x0007C000       /* 当前 FlashDB 扇区 */

#endif /* __FLASH_PARTITION_H__ */
