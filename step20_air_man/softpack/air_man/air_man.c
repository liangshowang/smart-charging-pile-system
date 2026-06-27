/**
 * air_man.c — 4G 模块管理层实现
 *
 * 在 at_dev 通用 AT 引擎之上, 封装 AIR724 的设备管理层:
 *   - send_str: 原始发送 (如发送纯数据, 不等 OK/ERROR)
 *   - clr_rxbuf: 清空缓冲 (准备接收新数据)
 *   - ptf_rxbuf: 打印缓冲 (调试)
 *   - AT/CPIN/CSQ 等: 便捷命令封装 (内部调 at_man->send_cmd,
 *     拿到 OK 后解析响应内容, 提取业务数据)
 *
 * 每个 AT 命令封装做三件事:
 *   1. 调 at_man->send_cmd() 发命令
 *   2. 从 dev->at->rxbuf 读取原始响应
 *   3. 解析响应字符串, 提取需要的字段
 *
 * 纯软件模块 — 不操作任何寄存器。
 */

#include "air_man.h"
#include "at_dev.h"
#include "drv_uart1.h"
#include "sysprt.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdio.h>

/* ---- 接收缓冲区大小 ---- */
#define AIR_RXBUF_SIZE  2048

/* ================================================================
 * 内部辅助函数
 * ================================================================ */

/* ---- copy_rxbuf — 将 at_dev 的响应拷贝到 air_dev 的缓冲 ---- */
static void copy_rxbuf(air_dev_pt dev)
{
    if (dev->at->rxlen > 0 && dev->at->rxlen < dev->rxsize) {
        memcpy(dev->rxbuf, dev->at->rxbuf, dev->at->rxlen);
        dev->rxlen = dev->at->rxlen;
        dev->rxbuf[dev->rxlen] = '\0';
    }
}

/* ---- find_kv — 在响应中查找 key: value 形式的字段
 *      如 "+CSQ: 20,5" 中查找 "+CSQ:", 返回后面的 "20,5" ---- */
static char* find_kv(const char *buf, const char *key)
{
    char *p = strstr(buf, key);
    if (p == NULL) return NULL;
    /* 跳过 key 本身, 跳过空格和冒号 */
    p += strlen(key);
    while (*p == ' ' || *p == ':') p++;
    return p;
}

/* ================================================================
 * 便捷 AT 命令封装
 * ================================================================ */

/* ---- do_AT — AT 基础联通测试 ---- */
static int do_AT(void *d)
{
    air_dev_pt dev = (air_dev_pt)d;
    int ret = at_man->send_cmd(dev->at, "AT", 500, 3000);
    copy_rxbuf(dev);
    return ret;  /* 1=OK */
}

/* ---- do_CPIN — 查询 SIM 卡状态
 *      响应: "+CPIN: READY" → result="READY" ---- */
static int do_CPIN(void *d, char *result, int maxlen)
{
    air_dev_pt dev = (air_dev_pt)d;
    char *p;

    int ret = at_man->send_cmd(dev->at, "AT+CPIN?", 500, 3000);
    copy_rxbuf(dev);

    if (ret != 1) return ret;

    /* 解析: 在响应中找 "+CPIN:" 后面的状态字符串 */
    p = find_kv((char *)dev->rxbuf, "+CPIN:");
    if (p && result && maxlen > 0) {
        /* 提取到行尾 (\r 或 \n) 或空格 */
        int i = 0;
        while (p[i] && p[i] != '\r' && p[i] != '\n' && i < maxlen - 1) {
            result[i] = p[i];
            i++;
        }
        result[i] = '\0';
    }
    return 1;
}

/* ---- do_CSQ — 查询信号质量
 *      响应: "+CSQ: 20,5" → rssi=20, ber=5
 *      rssi: 0~31 (越大信号越好, 99=无信号)
 *      ber:  0~7  (越小越好, 99=未知) ---- */
static int do_CSQ(void *d, int *rssi, int *ber)
{
    air_dev_pt dev = (air_dev_pt)d;
    char *p;

    int ret = at_man->send_cmd(dev->at, "AT+CSQ", 500, 3000);
    copy_rxbuf(dev);

    if (ret != 1) return ret;

    /* 解析: "+CSQ: 20,5" */
    p = find_kv((char *)dev->rxbuf, "+CSQ:");
    if (p) {
        if (rssi) *rssi = atoi(p);
        /* 找逗号 */
        p = strchr(p, ',');
        if (p && ber) *ber = atoi(p + 1);
    }
    return 1;
}

/* ---- do_CREG — 查询网络注册状态
 *      响应: "+CREG: 0,1" → stat=1 (已注册到归属网络) ---- */
static int do_CREG(void *d, int *stat)
{
    air_dev_pt dev = (air_dev_pt)d;
    char *p;

    int ret = at_man->send_cmd(dev->at, "AT+CREG?", 500, 3000);
    copy_rxbuf(dev);

    if (ret != 1) return ret;

    /* 解析: "+CREG: 0,1" — 取第二个数字 */
    p = find_kv((char *)dev->rxbuf, "+CREG:");
    if (p) {
        p = strchr(p, ',');
        if (p && stat) *stat = atoi(p + 1);
    }
    return 1;
}

/* ---- do_CGATT — 查询 GPRS 附着状态
 *      响应: "+CGATT: 1" → stat=1 (已附着) ---- */
static int do_CGATT(void *d, int *stat)
{
    air_dev_pt dev = (air_dev_pt)d;
    char *p;

    int ret = at_man->send_cmd(dev->at, "AT+CGATT?", 500, 5000);
    copy_rxbuf(dev);

    if (ret != 1) return ret;

    /* 解析: "+CGATT: 1" */
    p = find_kv((char *)dev->rxbuf, "+CGATT:");
    if (p && stat) *stat = atoi(p);
    return 1;
}

/* ================================================================
 * do_create_dev — 创建 4G 模块设备
 *
 * 1. 创建底层 at_dev
 * 2. 分配 air_dev 结构体 + 接收缓冲
 * 3. 注册 AT 命令封装函数指针
 * ================================================================ */
static air_dev_pt do_create_dev(drv_uart1_pt uart)
{
    air_dev_pt dev;
    at_dev_pt   at;

    /* 创建底层 AT 设备 */
    at = at_man->create(uart);
    if (at == NULL) {
        sysprt->aerr("[air_man] create at_dev failed\r\n");
        return NULL;
    }

    /* 分配 air_dev 结构体 */
    dev = (air_dev_pt)pvPortMalloc(sizeof(air_dev_t));
    if (dev == NULL) {
        sysprt->aerr("[air_man] malloc air_dev failed\r\n");
        at_man->delete_dev(at);
        return NULL;
    }
    memset(dev, 0, sizeof(air_dev_t));

    /* 分配接收缓冲区 */
    dev->rxbuf = (uint8_t *)pvPortMalloc(AIR_RXBUF_SIZE);
    if (dev->rxbuf == NULL) {
        sysprt->aerr("[air_man] malloc rxbuf failed\r\n");
        vPortFree(dev);
        at_man->delete_dev(at);
        return NULL;
    }
    memset(dev->rxbuf, 0, AIR_RXBUF_SIZE);
    dev->rxsize = AIR_RXBUF_SIZE;

    dev->at = at;

    /* 注册命令函数指针 */
    dev->AT     = do_AT;
    dev->CPIN   = do_CPIN;
    dev->CSQ    = do_CSQ;
    dev->CREG   = do_CREG;
    dev->CGATT  = do_CGATT;

    sysprt->alog("[air_man] AIR724 device created\r\n");
    return dev;
}

/* ================================================================
 * do_delete_dev — 销毁设备
 * ================================================================ */
static void do_delete_dev(air_dev_pt dev)
{
    if (dev == NULL) return;
    if (dev->at)   at_man->delete_dev(dev->at);
    if (dev->rxbuf) vPortFree(dev->rxbuf);
    vPortFree(dev);
    sysprt->alog("[air_man] device deleted\r\n");
}

/* ================================================================
 * do_send_str — 原始字符串发送
 *
 * 直接发送, 不等待响应, 不检查 OK/ERROR。
 * 用于发送纯数据 (如透传模式下的数据帧)。
 * ================================================================ */
static void do_send_str(air_dev_pt dev, const char *str)
{
    if (dev == NULL || dev->at == NULL) return;

    /* 发送字符串 + \r\n */
    dev->at->uart->send((const uint8_t *)str, strlen(str));
    dev->at->uart->send((const uint8_t *)"\r\n", 2);
}

/* ================================================================
 * do_clr_rxbuf — 清空接收缓冲
 * ================================================================ */
static void do_clr_rxbuf(air_dev_pt dev)
{
    if (dev == NULL) return;

    /* 清空 UART 硬件环形缓冲 */
    if (dev->at && dev->at->uart) {
        dev->at->uart->clear_rx_buf();
    }

    /* 清空 at_dev 缓冲 */
    if (dev->at) {
        memset(dev->at->rxbuf, 0, dev->at->rxsize);
        dev->at->rxlen = 0;
    }

    /* 清空 air_dev 缓冲 */
    memset(dev->rxbuf, 0, dev->rxsize);
    dev->rxlen = 0;
}

/* ================================================================
 * do_ptf_rxbuf — 打印接收缓冲区 (调试用)
 * ================================================================ */
static void do_ptf_rxbuf(air_dev_pt dev)
{
    uint32_t i;

    if (dev == NULL || dev->rxlen == 0) {
        sysprt->alog("[air_man] rxbuf empty\r\n");
        return;
    }

    sysprt->alog("[air_man] --- rxbuf (%lu bytes) ---\r\n", dev->rxlen);

    /* 文本格式 (可打印字符直接显示, 控制字符转义) */
    sysprt->alog("[air_man] text: ");
    for (i = 0; i < dev->rxlen; i++) {
        uint8_t c = dev->rxbuf[i];
        if (c >= 0x20 && c < 0x7F)
            printf("%c", c);
        else if (c == '\r') printf("\\r");
        else if (c == '\n') printf("\\n");
        else printf(".");
    }
    printf("\r\n");
}

/* ================================================================
 * 静态实例 + 导出指针
 * ================================================================ */
static air_man_t m_air_man = {
    do_create_dev,
    do_delete_dev,
    do_send_str,
    do_clr_rxbuf,
    do_ptf_rxbuf,
};

air_man_pt air_man = &m_air_man;

/* 全局 4G 模块设备指针 */
air_dev_pt air724 = NULL;
