/**
 * flashdb.c — 持久化配置存储实现
 *
 * 使用 SWM320 IAP 函数操作内部 Flash:
 *   FLASH_Erase(addr)       — 擦除 4KB 扇区
 *   FLASH_Write(addr,buf,n) — 写入 n 个 32-bit 字 (n 必须是 4 的倍数)
 *
 * 存储格式 (flash 扇区内):
 *   offset 0:   magic   (4B, 0xDBFCDBFC)
 *   offset 4:   storage_t 数据
 *   offset N:   checksum (4B, XOR of all words in storage_t)
 *
 * 写流程:
 *   1. 调用 Flash_Param_at_xMHz() 配置时序
 *   2. 构建写缓冲 (magic + data + checksum)
 *   3. 擦除扇区
 *   4. 写入缓冲
 *
 * 读流程:
 *   1. 读 magic 验证
 *   2. 复制数据
 *   3. 读 checksum 验证
 */

#include "flashdb.h"
#include "SWM320.h"
#include "SWM320_flash.h"
#include "sysprt.h"
#include <string.h>

/* ---- 写缓冲大小 ---- */
#define BUF_WORDS  ((sizeof(storage_t) / 4) + 2)  /* +1 magic, +1 checksum */

/* ---- RAM 中的配置副本 ---- */
static storage_t g_storage;

/* ---- 默认配置 ---- */
static const storage_t default_storage = {
    .boot_code = 0,
    .boot_app  = 0,
    .ota_req   = 0,
    .net_mode  = 0,
    .k         = { 1.0f, 1.0f },
    .b         = { 0.0f, 0.0f },
    .user_name = "synwit",
    .user_pwd  = "123456",
};

/* ================================================================
 * calc_checksum — 计算 storage_t 的 XOR checksum
 * ================================================================ */
static uint32_t calc_checksum(const storage_t *s)
{
    const uint32_t *p = (const uint32_t *)s;
    uint32_t sum = 0;
    uint32_t i;
    uint32_t n = sizeof(storage_t) / 4;

    for (i = 0; i < n; i++)
        sum ^= p[i];

    return sum;
}

/* ================================================================
 * do_init — 初始化 FlashDB
 *
 * 尝试从 Flash 加载配置:
 *   - magic 匹配 + checksum 通过 → 使用 Flash 中的配置
 *   - 否则 → 使用默认配置并写入 Flash
 * ================================================================ */
static void do_init(void)
{
    int ret;

    /* 配置 Flash 时序 (120MHz) */
    Flash_Param_at_xMHz(120);

    /* 尝试加载 — 如果失败则保存默认值 */
    ret = flashdb->load();

    if (ret == 0) {
        sysprt->alog("[flashdb] loaded from flash, k[0]=%.4f\r\n",
                     g_storage.k[0]);
    } else {
        sysprt->alog("[flashdb] flash invalid, writing defaults\r\n");
        memcpy(&g_storage, &default_storage, sizeof(storage_t));
        flashdb->save();
    }
}

/* ================================================================
 * do_save — 保存配置到 Flash
 *
 * 流程:
 *   1. 计算 checksum
 *   2. 构建写缓冲 (对齐到 16 字节)
 *   3. 擦除扇区
 *   4. 写入
 * ================================================================ */
static int do_save(void)
{
    uint32_t buf[BUF_WORDS];
    uint32_t words;
    uint32_t addr = FLASHDB_ADDR;

    /* 计算 checksum */
    uint32_t cs = calc_checksum(&g_storage);

    /* 构建缓冲: [magic] [storage_t] [checksum] */
    memset(buf, 0, sizeof(buf));
    buf[0] = FLASHDB_MAGIC;
    memcpy(&buf[1], &g_storage, sizeof(storage_t));
    buf[1 + sizeof(storage_t) / 4] = cs;

    /*
     * FLASH_Write 要求 count 为 4 的倍数 (每 4 个 32-bit word = 128bit)
     * buf 可能不是 4 的倍数, 需要向上取整
     */
    words = 1 + sizeof(storage_t) / 4 + 1;  /* magic + data + checksum */
    words = (words + 3) & ~3u;              /* 向上取整到 4 的倍数 */

    /* 擦除扇区 */
    sysprt->alog("[flashdb] erasing sector at 0x%08X...\r\n", addr);
    FLASH_Erase(addr);

    /* 写入 */
    sysprt->alog("[flashdb] writing %lu words...\r\n", words);
    if (FLASH_Write(addr, buf, words) != FLASH_RES_OK) {
        sysprt->aerr("[flashdb] write FAILED!\r\n");
        return -1;
    }

    sysprt->alog("[flashdb] saved OK\r\n");
    return 0;
}

/* ================================================================
 * do_load — 从 Flash 加载配置
 *
 * 验证:
 *   1. magic == FLASHDB_MAGIC
 *   2. checksum 匹配
 *
 * 返回值: 0=成功, -1=失败
 * ================================================================ */
static int do_load(void)
{
    uint32_t addr = FLASHDB_ADDR;
    uint32_t *flash_ptr = (uint32_t *)addr;
    uint32_t magic;
    uint32_t stored_cs;
    uint32_t calc_cs;

    /* 读 magic */
    magic = flash_ptr[0];
    if (magic != FLASHDB_MAGIC) {
        sysprt->alog("[flashdb] magic mismatch: 0x%08X\r\n", magic);
        return -1;
    }

    /* 读数据 */
    memcpy(&g_storage, &flash_ptr[1], sizeof(storage_t));

    /* 读 checksum (在数据之后) */
    stored_cs = flash_ptr[1 + sizeof(storage_t) / 4];

    /* 验证 checksum */
    calc_cs = calc_checksum(&g_storage);
    if (stored_cs != calc_cs) {
        sysprt->alog("[flashdb] checksum mismatch: "
                     "stored=0x%08X calc=0x%08X\r\n",
                     stored_cs, calc_cs);
        return -1;
    }

    return 0;
}

/* ================================================================
 * do_get — 获取配置指针
 * ================================================================ */
static storage_t* do_get(void)
{
    return &g_storage;
}

/* ================================================================
 * 静态实例 + 导出指针
 * ================================================================ */
static flashdb_t m_flashdb = {
    do_init,
    do_save,
    do_load,
    do_get,
};

flashdb_pt flashdb = &m_flashdb;
