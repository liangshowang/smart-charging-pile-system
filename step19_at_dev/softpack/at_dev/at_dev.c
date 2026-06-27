/**
 * at_dev.c — AT 命令引擎实现
 *
 * 核心思路:
 *   send_cmd() 是一个同步阻塞的状态机:
 *     1. 清空 UART RX 缓冲
 *     2. 发送 "AT+XXX\r\n"
 *     3. 循环轮询 UART RX 缓冲:
 *        a. 有新数据 → 追加到 dev->rxbuf, 检查是否收到完整行
 *        b. 完整行 → 检查 OK/ERROR, 返回结果
 *        c. 无新数据 → 任务延迟 1ms, 累加超时计数器
 *        d. 超时 → 返回 -1 或 -2
 *
 * 依赖:
 *   - drv_uart1 (发送 + 接收)
 *   - FreeRTOS (vTaskDelay 延迟; xTaskGetTickCount 计时)
 *   - sysprt (日志)
 *   - string.h (strstr 字符串匹配)
 *
 * 纯软件模块 — 不操作任何寄存器。
 */

#include "at_dev.h"
#include "drv_uart1.h"
#include "sysprt.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

/* ---- 接收缓冲区大小 ---- */
#define RXBUF_SIZE  2048

/* ---- 自动波特率尝试序列 ---- */
static const uint32_t baud_list[] = {
    115200,
    57600,
    38400,
    19200,
    9600,
};
#define BAUD_COUNT  (sizeof(baud_list) / sizeof(baud_list[0]))

/* ================================================================
 * do_create — 创建 AT 设备
 *
 * 分配设备结构体和接收缓冲区。
 * 绑定到指定 UART (uart1, 连接 4G 模块)。
 * ================================================================ */
static at_dev_pt do_create(drv_uart1_pt uart)
{
    at_dev_pt dev;

    /* 分配设备结构体 */
    dev = (at_dev_pt)pvPortMalloc(sizeof(at_dev_t));
    if (dev == NULL) {
        sysprt->aerr("[at_dev] create: malloc dev failed\r\n");
        return NULL;
    }
    memset(dev, 0, sizeof(at_dev_t));

    /* 分配接收缓冲区 */
    dev->rxbuf = (uint8_t *)pvPortMalloc(RXBUF_SIZE);
    if (dev->rxbuf == NULL) {
        sysprt->aerr("[at_dev] create: malloc rxbuf failed\r\n");
        vPortFree(dev);
        return NULL;
    }
    memset(dev->rxbuf, 0, RXBUF_SIZE);

    dev->rxsize = RXBUF_SIZE;
    dev->uart   = uart;

    sysprt->alog("[at_dev] device created, rxbuf=%u bytes\r\n", RXBUF_SIZE);
    return dev;
}

/* ================================================================
 * do_delete_dev — 销毁 AT 设备
 * ================================================================ */
static void do_delete_dev(at_dev_pt dev)
{
    if (dev == NULL) return;
    if (dev->rxbuf) vPortFree(dev->rxbuf);
    vPortFree(dev);
    sysprt->alog("[at_dev] device deleted\r\n");
}

/* ================================================================
 * send_str — 发送字符串到 AT 设备
 *
 * 使用 uart->send 批量发送 (先写入 TX 环形缓冲, 再一次性 kick)。
 * ================================================================ */
static void send_str(at_dev_pt dev, const char *str)
{
    dev->uart->send((const uint8_t *)str, strlen(str));
}

/* ================================================================
 * flush_rx — 清空 UART RX 缓冲和设备累积缓冲
 * ================================================================ */
static void flush_rx(at_dev_pt dev)
{
    dev->uart->clear_rx_buf();
    memset(dev->rxbuf, 0, dev->rxsize);
    dev->rxlen = 0;
}

/* ================================================================
 * do_send_cmd — 发送 AT 命令并等待响应
 *
 * 流程:
 *   1. 清空 RX 缓冲
 *   2. 发送 "AT+XXX\r\n"
 *   3. 循环轮询:
 *      - 从 UART RX 缓冲读数据到 dev->rxbuf
 *      - 有 \r\n → 检查 OK/ERROR
 *      - 无新数据 → vTaskDelay(1), 累加超时
 *
 * 参数:
 *   dto (data timeout): 连续无数据的最大等待时间 (ms)
 *   cto (command timeout): 整条命令的最大等待时间 (ms)
 *
 * 返回值:
 *    1 = OK, 0 = ERROR, -1 = 数据超时, -2 = 命令超时, 3 = 收到 '>'
 * ================================================================ */
static int do_send_cmd(at_dev_pt dev, const char *cmd, uint32_t dto, uint32_t cto)
{
    uint32_t err_ticks = 0;          /* 连续无数据的计数 (ms) */
    uint32_t start_tick;             /* 命令开始时刻 */
    int      rd_len;
    char    *ret_str;

    if (dev == NULL || dev->uart == NULL)
        return -2;

    /* 1. 清空 RX 缓冲 */
    flush_rx(dev);

    /* 2. 发送命令 */
    send_str(dev, cmd);
    send_str(dev, "\r\n");

    /* 给模块一点时间开始响应 */
    vTaskDelay(pdMS_TO_TICKS(50));

    /* 3. 开始轮询 */
    start_tick = xTaskGetTickCount();

    while (1) {
        /* 从 UART 环形缓冲读 1 字节, 追加到 dev->rxbuf */
        if (dev->rxlen < dev->rxsize - 1) {
            rd_len = dev->uart->read_rx_buf(
                &dev->rxbuf[dev->rxlen], 1);
        } else {
            rd_len = 0;  /* 缓冲区满, 丢弃 */
        }

        if (rd_len > 0) {
            dev->rxlen += rd_len;
            dev->rxbuf[dev->rxlen] = '\0';   /* 保持字符串结尾 */
            err_ticks = 0;                    /* 有数据, 重置无数据计数 */
        } else {
            /*
             * 没有新数据 — 检查是否已收到完整响应
             *
             * 判断依据: 收到 \r\n 说明模块已输出到行尾
             */
            ret_str = strstr((char *)dev->rxbuf, "\r\n");
            if (ret_str != NULL) {
                /* 收到了回车换行 → 检查响应内容 */
                if (strstr((char *)dev->rxbuf, "OK") != NULL) {
                    return 1;
                }
                if (strstr((char *)dev->rxbuf, "ERROR") != NULL) {
                    return 0;
                }
            }

            /* 检查透传提示符 '>' */
            if (dev->rxlen > 0 && dev->rxbuf[dev->rxlen - 1] == '>') {
                return 3;
            }

            /*
             * 数据超时处理
             *
             * dto == 0 或 dto == 0xFFFFFFFF:
             *   不启用数据超时 — 只靠 cto 兜底
             *   用于慢速响应命令 (如 AT+CGATT? 网络附着)
             *
             * dto > 0:
             *   连续 dto 毫秒无新数据 → 返回 -1
             *   用于快速响应命令 (如 AT, AT+CSQ)
             */
            if (dto == 0 || dto == 0xFFFFFFFF) {
                vTaskDelay(pdMS_TO_TICKS(10));
            } else {
                err_ticks++;
                vTaskDelay(pdMS_TO_TICKS(1));
                if (err_ticks > dto) {
                    sysprt->alog("[at_dev] data timeout, "
                                 "rxlen=%lu\r\n", dev->rxlen);
                    return -1;
                }
            }

            /*
             * 命令超时处理
             * 从发送开始计时, 超过 cto 毫秒 → 返回 -2
             */
            if (cto < (xTaskGetTickCount() - start_tick) * portTICK_PERIOD_MS) {
                sysprt->alog("[at_dev] command timeout, "
                             "cto=%lums, rxlen=%lu\r\n",
                             cto, dev->rxlen);
                return -2;
            }
            continue;
        }
    }
}

/* ================================================================
 * try_baud — 尝试单个波特率
 *
 * 重新初始化 UART, 发送 "AT", 检查模块是否有回显。
 * 模块收到 "AT" 后应该回显 "AT" 然后输出 "OK"。
 *
 * 返回: 0=匹配成功, -1=不匹配
 * ================================================================ */
static int try_baud(at_dev_pt dev, uint32_t baud)
{
    sysprt->alog("[at_dev] trying %lu bps...\r\n", baud);

    /* 重新初始化 UART 到新波特率 */
    dev->uart->init(baud);
    vTaskDelay(pdMS_TO_TICKS(100));

    /* 发送 AT 并等待响应 */
    flush_rx(dev);
    send_str(dev, "AT\r\n");
    vTaskDelay(pdMS_TO_TICKS(300));

    /* 读出所有已收到的数据 */
    {
        int total = 0;
        int n;
        while (1) {
            n = dev->uart->read_rx_buf(
                &dev->rxbuf[total],
                dev->rxsize - total - 1);
            if (n == 0) break;
            total += n;
        }
        dev->rxbuf[total] = '\0';
        dev->rxlen = total;
    }

    /* 检查回显: 模块应该回显 "AT" */
    if (strstr((char *)dev->rxbuf, "AT") != NULL ||
        strstr((char *)dev->rxbuf, "OK") != NULL) {
        sysprt->alog("[at_dev] auto baud OK at %lu bps\r\n", baud);
        return 0;
    }

    sysprt->alog("[at_dev] no response at %lu bps\r\n", baud);
    return -1;
}

/* ================================================================
 * do_auto_baud — 自动波特率匹配
 *
 * 遍历常见波特率序列, 找到模块实际使用的波特率。
 *
 * 注意: 会多次调用 uart->init() 重新配置硬件。
 *
 * 返回: 0=成功匹配, -1=所有波特率都不匹配
 * ================================================================ */
static int do_auto_baud(at_dev_pt dev, uint32_t cto)
{
    uint32_t i;
    int ret;

    (void)cto;  /* 当前版本未使用, 保留接口兼容 */

    for (i = 0; i < BAUD_COUNT; i++) {
        ret = try_baud(dev, baud_list[i]);
        if (ret == 0)
            return 0;
    }

    sysprt->aerr("[at_dev] auto baud FAILED, all rates tried\r\n");
    return -1;
}

/* ================================================================
 * do_dump_rx — 打印接收缓冲区 (调试用)
 *
 * 以文本和 hex 两种格式打印, 方便排查 AT 通信问题。
 * ================================================================ */
static void do_dump_rx(at_dev_pt dev)
{
    uint32_t i;

    if (dev == NULL || dev->rxlen == 0) {
        sysprt->alog("[at_dev] rxbuf empty\r\n");
        return;
    }

    sysprt->alog("[at_dev] --- rxbuf (%lu bytes) ---\r\n", dev->rxlen);

    /* 文本格式 */
    sysprt->alog("[at_dev] text: ");
    for (i = 0; i < dev->rxlen; i++) {
        if (dev->rxbuf[i] >= 0x20 && dev->rxbuf[i] < 0x7F)
            printf("%c", dev->rxbuf[i]);
        else if (dev->rxbuf[i] == '\r')
            printf("\\r");
        else if (dev->rxbuf[i] == '\n')
            printf("\\n");
        else
            printf(".");
    }
    printf("\r\n");

    /* Hex 格式 (仅前 128 字节) */
    sysprt->alog("[at_dev] hex: ");
    for (i = 0; i < dev->rxlen && i < 128; i++) {
        printf("%02X ", dev->rxbuf[i]);
        if ((i + 1) % 32 == 0) printf("\r\n      ");
    }
    printf("\r\n");
}

/* ================================================================
 * 静态实例 + 导出指针
 * ================================================================ */
static at_man_t m_at_man = {
    do_create,
    do_delete_dev,
    do_send_cmd,
    do_auto_baud,
    do_dump_rx,
};

at_man_pt at_man = &m_at_man;

/* 全局 4G 模块设备指针 (初始为空, init 时创建) */
at_dev_pt air724 = NULL;
