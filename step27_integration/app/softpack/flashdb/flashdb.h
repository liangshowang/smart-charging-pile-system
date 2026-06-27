/**
 * flashdb.h — 持久化配置存储接口
 *
 * 使用 SWM320 内部 Flash 最后 4KB 扇区存储配置数据,
 * 断电不丢失。
 *
 * Flash 布局 (512KB total):
 *   Step 25 统一分区, 地址定义在 flash_partition.h
 *   0x7C000 ~ 0x7CFFF  : FLASHDB_ADDR (扇区 124/128)
 *
 * 存储格式:
 *   [4B magic 0xDBFCDBFC] [storage_t 数据] [4B checksum]
 *
 * 接口指针模式:
 *   flashdb → m_flashdb → { init, save, load, get }
 */

#ifndef __FLASHDB_H__
#define __FLASHDB_H__

#include <stdint.h>
#include "flash_partition.h"

/* ---- 存储地址 ---- */
/* FLASHDB_ADDR 定义在 flash_partition.h, 此处不再重复定义 */
#define FLASHDB_SECTOR    0x1000     /* 4KB 扇区大小 */
#define FLASHDB_MAGIC     0xDBFCDBFC /* 魔数 */

/* ---- 存储数据结构 ---- */
typedef struct {
    uint32_t boot_code;          /* 启动代码 */
    uint32_t boot_app;           /* 启动 APP 编号 (0/1) */
    uint32_t ota_req;            /* OTA 请求标志 */
    uint32_t net_mode;           /* 网络模式 */
    float    k[2];               /* HLW8012 校准系数 k (插座0/1) */
    float    b[2];               /* HLW8012 校准系数 b (插座0/1) */
    char     user_name[32];      /* 用户名 (服务器登录) */
    char     user_pwd[32];       /* 密码 (服务器登录) */
} storage_t;

/* ---- 操作接口 ---- */
typedef struct {
    void       (*init) (void);             /* 初始化: 从 Flash 加载到 RAM */
    int        (*save) (void);             /* 保存: 将 RAM 写入 Flash */
    int        (*load) (void);             /* 加载: 从 Flash 读取到 RAM */
    storage_t* (*get)  (void);             /* 获取 RAM 中的存储指针 */
} flashdb_t, *flashdb_pt;

/* ---- 全局接口指针 ---- */
extern flashdb_pt flashdb;

#endif /* __FLASHDB_H__ */
