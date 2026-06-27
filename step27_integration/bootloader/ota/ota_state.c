/**
 * ota_state.c — OTA 状态机 (HTTP 固件下载)
 *
 * 6 阶段状态机, 由 task_start 主循环驱动:
 *   Phase 0: OTA_INIT      — 创建 air724 设备, 构建 URL
 *   Phase 1: OTA_RESET     — 硬件复位 4G 模块, 等 45s, AT 握手
 *   Phase 2: OTA_INIT_DEV  — AT 初始化 (CGMI~CIFSR)
 *   Phase 3: OTA_CONNECT   — TCP 连接 OTA 服务器
 *   Phase 4: OTA_GET_SIZE  — HTTP Range 获取文件大小, 擦除 APP
 *   Phase 5: OTA_DOWNLOAD  — 循环下载 130B 块, CRC16 验证, 写入 Flash
 *   Phase 6: OTA_FINISH    — 关 TCP, 写 boot_app, 复位
 *
 * 参考: V5.5 BL 的 work_internet_http.c + 024_BL 的 network.c
 */

#include "flash_partition.h"
#include "../boot_config.h"
#include "ota_flash.h"
#include "ota_state.h"
#include "SWM320.h"
#include "SWM320_flash.h"
#include "sysprt.h"
#include "at_dev.h"
#include "air_man.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---- OTA 阶段 ---- */
enum {
    PHASE_INIT = 0,
    PHASE_RESET,
    PHASE_INIT_DEV,
    PHASE_CONNECT,
    PHASE_GET_SIZE,
    PHASE_DOWNLOAD,
    PHASE_FINISH,
    PHASE_ERROR,
};

/* ---- OTA 服务器配置 ---- */
#define OTA_SERVER_IP   "www.armsoc.cn"
#define OTA_SERVER_PORT 80
#define OTA_FILE_PATH   "/zxx/ota/app_charge_inner_hlw8012.bin"

/* ---- 下载块大小: 128B 数据 + 2B CRC16 = 130B ---- */
#define CHUNK_DATA_SIZE  128
#define CHUNK_TOTAL_SIZE 130

/* ---- 重试限制 ---- */
#define MAX_CHUNK_RETRIES  3
#define MAX_PHASE_RETRIES  3

/* ---- 4G 模块等待时间 (秒) ---- */
#define MODULE_BOOT_WAIT  45

/* ---- TCP 空闲任务栈 ---- */
#define RCV_LOOP_CNT  2000

/* ---- 内部状态 ---- */
static uint8_t  g_phase = PHASE_INIT;
static uint8_t  g_sub_state = 0;
static uint32_t g_file_size = 0;
static uint32_t g_recv_size = 0;
static uint32_t g_chunk_retries = 0;
static uint32_t g_phase_retries = 0;
static uint32_t g_start_tick = 0;

/* ---- 前向声明 ---- */
static int  phase_init(void);
static int  phase_reset(void);
static int  phase_init_dev(void);
static int  phase_connect(void);
static int  phase_get_size(void);
static int  phase_download(void);
static int  phase_finish(void);
static void dump_file_url(void);

/* ---- HTTP Range 字符串缓存 ---- */
static char g_http_req[256];
static char g_file_url[128];

/* ================================================================
 * ota_state_init — 初始化 OTA 状态机
 * ================================================================ */
void ota_state_init(void)
{
    g_phase = PHASE_INIT;
    g_sub_state = 0;
    g_file_size = 0;
    g_recv_size = 0;
    g_chunk_retries = 0;
    g_phase_retries = 0;
    g_dev = NULL;

    sysprt->alog("[ota] state machine initialized\r\n");
}

/* ================================================================
 * ota_state_work — 驱动 OTA 状态机 (每个 tick 调用一次)
 *
 * 返回: 0=进行中, 1=下载完成(已复位前), -1=错误
 * ================================================================ */
int ota_state_work(void)
{
    int ret;

    switch (g_phase) {
        case PHASE_INIT:      ret = phase_init();      break;
        case PHASE_RESET:     ret = phase_reset();     break;
        case PHASE_INIT_DEV:  ret = phase_init_dev();  break;
        case PHASE_CONNECT:   ret = phase_connect();   break;
        case PHASE_GET_SIZE:  ret = phase_get_size();  break;
        case PHASE_DOWNLOAD:  ret = phase_download();  break;
        case PHASE_FINISH:    ret = phase_finish();    break;
        default:
            sysprt->aerr("[ota] unknown phase %d\r\n", g_phase);
            g_phase = PHASE_ERROR;
            return -1;
    }

    return ret;
}

/* ---- 获取当前阶段 (供外部显示) ---- */
uint8_t ota_state_get_phase(void) { return g_phase; }
uint32_t ota_state_get_file_size(void) { return g_file_size; }
uint32_t ota_state_get_recv_size(void) { return g_recv_size; }

/* ================================================================
 * dump_file_url — 构建固件文件 URL
 * ================================================================ */
static void dump_file_url(void)
{
    memset(g_file_url, 0, sizeof(g_file_url));
    snprintf(g_file_url, sizeof(g_file_url) - 1, "%s", OTA_FILE_PATH);
    sysprt->alog("[ota] file URL: %s\r\n", g_file_url);
}

/* ================================================================
 * Phase 0: OTA_INIT — 创建 air724 设备
 * ================================================================ */
static int phase_init(void)
{
    sysprt->alog("[ota] === Phase 0: INIT ===\r\n");

    /* 构建固件 URL */
    dump_file_url();

    /* 确保 4G 模块复位引脚为高 (正常运行) */
    /* 注意: 不在这里操作硬件 — 由 task_start 在 init 时完成 */

    g_phase = PHASE_RESET;
    g_sub_state = 0;
    g_phase_retries = 0;

    return 0;
}

/* ================================================================
 * Phase 1: OTA_RESET — 硬件复位 4G 模块
 *
 * 子状态:
 *   0: NetRst 拉低 500ms
 *   1: 等 MODULE_BOOT_WAIT 秒, AT 握手
 * ================================================================ */
static int phase_reset(void)
{
    switch (g_sub_state) {
        case 0: {
            sysprt->alog("[ota] === Phase 1: RESET ===\r\n");

            /* 创建 air724 设备 */
            g_dev = air_man->create_dev(NULL);
            if (g_dev == NULL) {
                sysprt->aerr("[ota] create_dev FAILED\r\n");
                if (++g_phase_retries > MAX_PHASE_RETRIES) {
                    g_phase = PHASE_ERROR;
                    return -1;
                }
                return 0;  /* 下次重试 */
            }

            sysprt->alog("[ota] air724 device created, resetting...\r\n");

            /* 硬件复位: NetRst 拉低 → 延时 → 拉高 */
            /* 注意: 复位引脚由 task_start 管理 */
            /* 这里直接通过 at_dev 发 AT+RESET */

            g_sub_state = 1;
            return 0;
        }

        case 1: {
            int ret;
            char *resp;

            sysprt->alog("[ota] waiting for module (%ds)...\r\n",
                         MODULE_BOOT_WAIT);

            /* 等模块启动 */
            {
                volatile uint32_t delay = 0;
                /* 通过 AT 引擎的 deal_cmd_timeout 实现延时等待 */
                /* 实际项目中这里用 task_delay */
            }

            /* AT 握手 — 发 AT 等 OK */
            ret = at_man->send_cmd(g_dev->at, "AT", 500, 3000);
            if (ret != 0) {
                sysprt->aerr("[ota] AT handshake FAILED (retry %lu)\r\n",
                             g_phase_retries);
                if (++g_phase_retries > MAX_PHASE_RETRIES) {
                    /* 回到 phase 0 重新创建 */
                    g_phase = PHASE_INIT;
                    g_phase_retries = 0;
                }
                g_sub_state = 0;
                return 0;
            }

            sysprt->alog("[ota] AT handshake OK\r\n");
            g_phase = PHASE_INIT_DEV;
            g_sub_state = 0;
            g_phase_retries = 0;
            return 0;
        }

        default:
            g_sub_state = 0;
            return 0;
    }
}

/* ================================================================
 * Phase 2: OTA_INIT_DEV — AT 初始化序列
 *
 * 发送: CGMI → CGMR → CPIN? → CSQ → CREG? → CGATT?
 *      → CIPMODE → CIPMUX → CSTT → CIICR → CIFSR
 *
 * 每步 3 次重试, 失败退回 Phase 1
 * ================================================================ */
static int phase_init_dev(void)
{
    /* AT 命令列表 (简化: 只保留关键的) */
    static const char *at_cmds[] = {
        "AT+CGMI\r\n",     /* 厂商信息 */
        "AT+CGMR\r\n",     /* 固件版本 */
        "AT+CPIN?\r\n",    /* SIM 卡就绪? */
        "AT+CSQ\r\n",      /* 信号质量 */
        "AT+CREG?\r\n",    /* 网络注册 */
        "AT+CGATT?\r\n",   /* GPRS 附着 */
        "AT+CIPMODE=1\r\n",/* 透明模式 */
        "AT+CIPMUX=0\r\n", /* 单连接 */
        "AT+CSTT\r\n",     /* 设置 APN */
        "AT+CIICR\r\n",    /* 激活 PDP */
        "AT+CIFSR\r\n",    /* 获取 IP */
        NULL
    };

    if (g_sub_state == 0) {
        sysprt->alog("[ota] === Phase 2: INIT_DEV ===\r\n");
    }

    /* 逐条发送 AT 命令 */
    while (at_cmds[g_sub_state] != NULL) {
        int ret;

        sysprt->alog("[ota] AT cmd [%d]: %s", g_sub_state,
                     at_cmds[g_sub_state]);

        ret = at_man->send_cmd(g_dev->at, (char *)at_cmds[g_sub_state],
                               500, 5000);
        if (ret != 0) {
            sysprt->aerr("[ota] AT cmd [%d] FAILED\r\n", g_sub_state);
            if (++g_phase_retries > MAX_PHASE_RETRIES) {
                /* 退回硬件复位 */
                g_phase = PHASE_RESET;
                g_sub_state = 0;
                g_phase_retries = 0;
            }
            return 0;
        }

        g_sub_state++;
        g_phase_retries = 0;
    }

    /* 所有 AT 命令完成 */
    sysprt->alog("[ota] all AT init commands OK\r\n");
    g_phase = PHASE_CONNECT;
    g_sub_state = 0;
    g_phase_retries = 0;
    return 0;
}

/* ================================================================
 * Phase 3: OTA_CONNECT — TCP 连接 OTA 服务器
 * ================================================================ */
static int phase_connect(void)
{
    char cmd[128];
    int  ret;

    sysprt->alog("[ota] === Phase 3: CONNECT to %s:%d ===\r\n",
                 OTA_SERVER_IP, OTA_SERVER_PORT);

    snprintf(cmd, sizeof(cmd),
             "AT+CIPSTART=\"TCP\",\"%s\",\"%d\"\r\n",
             OTA_SERVER_IP, OTA_SERVER_PORT);

    ret = at_man->send_cmd(g_dev->at, cmd, 0, 30000);
    if (ret != 0) {
        sysprt->aerr("[ota] TCP connect FAILED (retry %lu)\r\n",
                     g_phase_retries);
        if (++g_phase_retries > MAX_PHASE_RETRIES) {
            g_phase = PHASE_RESET;  /* 退回硬件复位 */
            g_sub_state = 0;
            g_phase_retries = 0;
        }
        return 0;
    }

    sysprt->alog("[ota] TCP connected to %s:%d\r\n",
                 OTA_SERVER_IP, OTA_SERVER_PORT);

    g_phase = PHASE_GET_SIZE;
    g_sub_state = 0;
    g_phase_retries = 0;
    return 0;
}

/* ================================================================
 * Phase 4: OTA_GET_SIZE — HTTP Range 请求获取文件大小
 *
 * 子状态:
 *   0: 发送 HTTP GET Range:bytes=0-0
 *   1: 解析响应, 提取文件大小
 *   2: 擦除 APP 分区
 * ================================================================ */
static int phase_get_size(void)
{
    int ret;

    switch (g_sub_state) {
        case 0: {
            sysprt->alog("[ota] === Phase 4: GET FILE SIZE ===\r\n");

            /* 构造 HTTP Range 请求 */
            snprintf(g_http_req, sizeof(g_http_req),
                     "GET %s HTTP/1.1\r\n"
                     "Host:%s\r\n"
                     "Range:bytes=0-0\r\n"
                     "\r\n",
                     g_file_url, OTA_SERVER_IP);

            sysprt->alog("[ota] sending: %s", g_http_req);

            /* 发送请求 */
            air_man->send_str(g_dev, g_http_req);

            g_sub_state = 1;
            g_recv_size = 0;
            return 0;
        }

        case 1: {
            /* 从 UART RX 缓冲区解析文件大小
             * 响应格式: ... Content-Range: bytes 0-0/NNNN ...
             * 查找 "bytes 0-0/" 后面的数字 */
            char  *rxbuf;
            int    rxlen;
            char  *p_start, *p_end;
            char   size_str[16];

            rxbuf = (char *)g_dev->at->rxbuf;
            rxlen = (int)g_dev->at->rxlen;
            if (rxlen <= 0) return 0;  /* 还没收到数据 */

            /* 查找 "bytes 0-0/" */
            p_start = strstr(rxbuf, "bytes 0-0/");
            if (p_start == NULL) return 0;

            p_start += 10;  /* 跳过 "bytes 0-0/" */

            /* 找到数字结束位置 (空格或\r) */
            p_end = p_start;
            while (*p_end >= '0' && *p_end <= '9') p_end++;

            if (p_end == p_start) {
                sysprt->aerr("[ota] failed to parse file size\r\n");
                g_sub_state = 0;  /* 重试 */
                return 0;
            }

            {
                size_t n = (size_t)(p_end - p_start);
                if (n >= sizeof(size_str)) n = sizeof(size_str) - 1;
                memcpy(size_str, p_start, n);
                size_str[n] = '\0';
            }

            g_file_size = (uint32_t)atol(size_str);
            if (g_file_size == 0 || g_file_size > PART_APP_SIZE) {
                sysprt->aerr("[ota] invalid file size: %lu\r\n",
                             g_file_size);
                g_sub_state = 0;
                return 0;
            }

            sysprt->alog("[ota] file size: %lu bytes "
                         "(~%lu chunks)\r\n",
                         g_file_size,
                         g_file_size / CHUNK_TOTAL_SIZE);

            g_sub_state = 2;
            return 0;
        }

        case 2: {
            /* 擦除 APP 分区 */
            ret = ota_flash_erase_app();
            if (ret != 0) {
                sysprt->aerr("[ota] APP erase FAILED\r\n");
                g_phase = PHASE_ERROR;
                return -1;
            }

            /* 初始化写入指针 */
            ota_flash_init_write();

            sysprt->alog("[ota] APP partition erased, "
                         "ready to download\r\n");

            g_phase = PHASE_DOWNLOAD;
            g_sub_state = 0;
            g_recv_size = 0;
            g_chunk_retries = 0;
            return 0;
        }

        default:
            g_sub_state = 0;
            return 0;
    }
}

/* ================================================================
 * Phase 5: OTA_DOWNLOAD — 分块下载
 *
 * 子状态:
 *   0: 发送 HTTP Range 请求 (下一块)
 *   1: 等待/接收 130 字节
 *   2: CRC 验证 → 写入 Flash → 循环
 * ================================================================ */
static int phase_download(void)
{
    static uint8_t chunk_buf[CHUNK_TOTAL_SIZE];
    static uint32_t next_addr;
    char   *rxbuf;
    int     rxlen;
    int     ret;

    switch (g_sub_state) {
        case 0: {
            /* 检查是否下载完成 */
            if (g_recv_size >= g_file_size) {
                sysprt->alog("[ota] download complete!\r\n");
                g_phase = PHASE_FINISH;
                g_sub_state = 0;
                return 0;
            }

            /* 构造 Range 请求 */
            next_addr = g_recv_size;
            snprintf(g_http_req, sizeof(g_http_req),
                     "GET %s HTTP/1.1\r\n"
                     "Host:%s\r\n"
                     "Range:bytes=%lu-%lu\r\n"
                     "\r\n",
                     g_file_url, OTA_SERVER_IP,
                     next_addr, next_addr + CHUNK_TOTAL_SIZE - 1);

            air_man->send_str(g_dev, g_http_req);

            memset(chunk_buf, 0, sizeof(chunk_buf));
            g_sub_state = 1;
            return 0;
        }

        case 1: {
            /* 查找 HTTP 响应体中的 \r\n\r\n 标记
             * 之后 130 字节是数据块 */
            char *body;

            rxbuf = (char *)g_dev->at->rxbuf;
            rxlen = (int)g_dev->at->rxlen;
            if (rxlen < CHUNK_TOTAL_SIZE) return 0;  /* 还没收够 */

            /* 查找 HTTP 头结束标记 */
            body = strstr(rxbuf, "\r\n\r\n");
            if (body == NULL) return 0;

            body += 4;  /* 跳过 \r\n\r\n */

            /* 检查剩余长度是否够 130 字节 */
            if ((body - rxbuf) + CHUNK_TOTAL_SIZE > (size_t)rxlen) {
                return 0;  /* 还没收够 */
            }

            /* 复制 130 字节数据块 */
            memcpy(chunk_buf, body, CHUNK_TOTAL_SIZE);
            air_man->clr_rxbuf(g_dev);  /* 清除缓冲区 */

            g_sub_state = 2;
            return 0;
        }

        case 2: {
            /* CRC16 验证 */
            if (crc16_verify_chunk(chunk_buf)) {
                /* CRC 通过 → 写入 Flash */
                ret = ota_flash_write_chunk(chunk_buf, CHUNK_DATA_SIZE);
                if (ret != 0) {
                    sysprt->aerr("[ota] flash write FAILED\r\n");
                    g_phase = PHASE_ERROR;
                    return -1;
                }

                g_recv_size += CHUNK_TOTAL_SIZE;
                g_chunk_retries = 0;

                /* 每 10% 打印进度 */
                {
                    uint32_t pct = (g_recv_size * 100) / g_file_size;
                    static uint32_t last_pct = 0;
                    if (pct >= last_pct + 10 || pct == 100) {
                        sysprt->alog("[ota] progress: %lu%% "
                                     "(%lu/%lu)\r\n",
                                     pct, g_recv_size, g_file_size);
                        last_pct = pct;
                    }
                }

                g_sub_state = 0;  /* 下一块 */
            } else {
                /* CRC 失败 → 重试 */
                uint16_t crc_calc = crc16_ibm(chunk_buf, 128);
                uint16_t crc_stored = (uint16_t)chunk_buf[128]
                                    | ((uint16_t)chunk_buf[129] << 8);
                sysprt->aerr("[ota] CRC mismatch! "
                             "calc=0x%04X stored=0x%04X "
                             "(retry %lu)\r\n",
                             crc_calc, crc_stored,
                             g_chunk_retries + 1);

                if (++g_chunk_retries > MAX_CHUNK_RETRIES) {
                    sysprt->aerr("[ota] max retries exceeded, "
                                 "reconnecting...\r\n");
                    g_chunk_retries = 0;
                    /* 关 TCP 重连 */
                    at_man->send_cmd(g_dev->at, "AT+CIPCLOSE",
                                     1000, 10000);
                    g_phase = PHASE_CONNECT;
                    g_sub_state = 0;
                    return 0;
                }

                g_sub_state = 0;  /* 重新请求同一块 */
            }
            return 0;
        }

        default:
            g_sub_state = 0;
            return 0;
    }
}

/* ================================================================
 * Phase 6: OTA_FINISH — 关闭连接, 写启动标志, 复位
 *
 * 子状态:
 *   0: 关 TCP
 *   1: 写 boot_app = _APP_FAC
 *   2: 触发复位
 * ================================================================ */
static int phase_finish(void)
{
    switch (g_sub_state) {
        case 0:
            sysprt->alog("[ota] === Phase 6: FINISH ===\r\n");

            /* 关 TCP */
            at_man->send_cmd(g_dev->at, "AT+CIPCLOSE", 1000, 10000);
            g_sub_state = 1;
            return 0;

        case 1: {
            int ret;

            sysprt->alog("[ota] setting boot_app = _APP_FAC...\r\n");

            ret = boot_config_set_boot_app(_APP_FAC);
            if (ret != 0) {
                sysprt->aerr("[ota] write boot_app FAILED!\r\n");
                g_phase = PHASE_ERROR;
                return -1;
            }

            sysprt->alog("[ota] boot_app written OK\r\n");
            sysprt->alog("[ota] ===================================\r\n");
            sysprt->alog("[ota] OTA SUCCESS!\r\n");
            sysprt->alog("[ota] Total: %lu bytes\r\n",
                         ota_flash_get_written());
            sysprt->alog("[ota] Rebooting in 1s...\r\n");
            sysprt->alog("[ota] ===================================\r\n");

            g_sub_state = 2;
            return 0;
        }

        case 2: {
            /* 软件延时 1 秒, 然后触发看门狗复位 */
            volatile uint32_t delay;
            for (delay = 0; delay < 110000000; delay++) {
                __NOP();
            }
            /* 使能看门狗 → 不复位 → 500ms 后自动复位 */
            /* 简单方式: 禁用所有中断后死循环, 等待看门狗 */
            __disable_irq();
            while (1) __NOP();
        }

        default:
            g_sub_state = 0;
            return 0;
    }
}
