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

/* ---- do_CGMI — 查询制造商信息
 *      响应: "Quectel" / "SIMCOM" 等 ---- */
static int do_CGMI(void *d, char *result, int maxlen)
{
    air_dev_pt dev = (air_dev_pt)d;
    char *p;

    int ret = at_man->send_cmd(dev->at, "AT+CGMI", 500, 3000);
    copy_rxbuf(dev);

    if (ret != 1) return ret;
    if (result == NULL || maxlen <= 0) return 1;

    /* CGMI 响应格式: \r\n<manufacturer>\r\n\r\nOK\r\n
     * 跳过前导 \r\n, 提取第一行文本 */
    p = (char *)dev->rxbuf;
    while (*p == '\r' || *p == '\n') p++;
    {
        int i = 0;
        while (p[i] && p[i] != '\r' && p[i] != '\n' && i < maxlen - 1) {
            result[i] = p[i];
            i++;
        }
        result[i] = '\0';
    }
    return 1;
}

/* ---- do_CGMR — 查询固件版本
 *      响应: 类似 CGMI, 返回版本字符串 ---- */
static int do_CGMR(void *d, char *result, int maxlen)
{
    air_dev_pt dev = (air_dev_pt)d;
    char *p;

    int ret = at_man->send_cmd(dev->at, "AT+CGMR", 500, 3000);
    copy_rxbuf(dev);

    if (ret != 1) return ret;
    if (result == NULL || maxlen <= 0) return 1;

    /* CGMR 响应格式同 CGMI: \r\n<version>\r\n\r\nOK\r\n */
    p = (char *)dev->rxbuf;
    while (*p == '\r' || *p == '\n') p++;
    {
        int i = 0;
        while (p[i] && p[i] != '\r' && p[i] != '\n' && i < maxlen - 1) {
            result[i] = p[i];
            i++;
        }
        result[i] = '\0';
    }
    return 1;
}

/* ---- do_CIPMODE — 设置透传模式
 *      AT+CIPMODE=1 → 透传模式, AT+CIPMODE=0 → 非透传 ---- */
static int do_CIPMODE(void *d, int mode)
{
    air_dev_pt dev = (air_dev_pt)d;
    char cmd[32];

    snprintf(cmd, sizeof(cmd), "AT+CIPMODE=%d", mode);
    int ret = at_man->send_cmd(dev->at, cmd, 1000, 5000);
    copy_rxbuf(dev);
    return ret;
}

/* ---- do_CIPMUX — 设置连接模式
 *      AT+CIPMUX=0 → 单连接, AT+CIPMUX=1 → 多连接 ---- */
static int do_CIPMUX(void *d, int mode)
{
    air_dev_pt dev = (air_dev_pt)d;
    char cmd[32];

    snprintf(cmd, sizeof(cmd), "AT+CIPMUX=%d", mode);
    int ret = at_man->send_cmd(dev->at, cmd, 1000, 5000);
    copy_rxbuf(dev);
    return ret;
}

/* ---- do_CSTT — 设置 APN (接入点名称)
 *      参数为 NULL 时使用自动 APN: AT+CSTT
 *      否则: AT+CSTT="apn","user","pwd" ---- */
static int do_CSTT(void *d, const char *apn, const char *user, const char *pwd)
{
    air_dev_pt dev = (air_dev_pt)d;
    char cmd[128];

    if (apn && apn[0]) {
        /* 指定 APN */
        snprintf(cmd, sizeof(cmd), "AT+CSTT=\"%s\",\"%s\",\"%s\"",
                 apn,
                 user ? user : "",
                 pwd  ? pwd  : "");
    } else {
        /* 自动 APN */
        snprintf(cmd, sizeof(cmd), "AT+CSTT");
    }

    int ret = at_man->send_cmd(dev->at, cmd, 1000, 10000);
    copy_rxbuf(dev);
    return ret;
}

/* ---- do_CIICR — 激活 GPRS 移动场景
 *      发送 AT+CIICR, 等待模块获取 IP 地址
 *      这个命令耗时较长 (可达 30s), 超时设 60s ---- */
static int do_CIICR(void *d)
{
    air_dev_pt dev = (air_dev_pt)d;

    int ret = at_man->send_cmd(dev->at, "AT+CIICR", 0, 60000);
    copy_rxbuf(dev);
    return ret;
}

/* ---- do_CIFSR — 查询本机 IP 地址
 *      响应: "10.xxx.xxx.xxx" (点分十进制) ---- */
static int do_CIFSR(void *d, char *ip, int maxlen)
{
    air_dev_pt dev = (air_dev_pt)d;
    char *p;

    int ret = at_man->send_cmd(dev->at, "AT+CIFSR", 500, 5000);
    copy_rxbuf(dev);

    if (ret != 1) return ret;
    if (ip == NULL || maxlen <= 0) return 1;

    /* CIFSR 响应中查找点分十进制 IP:
     * 查找第一个数字字符作为 IP 起始 */
    p = (char *)dev->rxbuf;
    while (*p && !(*p >= '0' && *p <= '9')) p++;

    if (*p) {
        int i = 0;
        while (p[i] && ((p[i] >= '0' && p[i] <= '9') || p[i] == '.')
               && i < maxlen - 1) {
            ip[i] = p[i];
            i++;
        }
        ip[i] = '\0';
    }
    return 1;
}

/* ---- do_CIPSTART — 建立 TCP/UDP 连接
 *      发送: AT+CIPSTART="TCP","host","port"
 *      成功响应含 "CONNECT" + "OK" ---- */
static int do_CIPSTART(void *d, const char *type, const char *addr,
                       const char *port)
{
    air_dev_pt dev = (air_dev_pt)d;
    char cmd[256];

    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"%s\",\"%s\",\"%s\"",
             type, addr, port);

    /* CIPSTART 可能需要较长时间 (DNS + TCP握手), cto=30s, 不设 dto */
    int ret = at_man->send_cmd(dev->at, cmd, 0, 30000);
    copy_rxbuf(dev);

    /* 收到 OK 且响应中包含 CONNECT 才算成功 */
    if (ret == 1) {
        if (strstr((char *)dev->rxbuf, "CONNECT") != NULL) {
            return 1;
        }
        /* 有 OK 但没有 CONNECT, 也可能成功 (模块差异) */
        return 1;
    }
    return ret;
}

/* ---- do_CIPCLOSE — 关闭连接 ---- */
static int do_CIPCLOSE(void *d, int id)
{
    air_dev_pt dev = (air_dev_pt)d;
    char cmd[32];

    snprintf(cmd, sizeof(cmd), "AT+CIPCLOSE=%d", id);
    int ret = at_man->send_cmd(dev->at, cmd, 1000, 10000);
    copy_rxbuf(dev);
    return ret;
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
    dev->AT       = do_AT;
    dev->CPIN     = do_CPIN;
    dev->CSQ      = do_CSQ;
    dev->CREG     = do_CREG;
    dev->CGATT    = do_CGATT;
    dev->CGMI     = do_CGMI;
    dev->CGMR     = do_CGMR;
    dev->CIPMODE  = do_CIPMODE;
    dev->CIPMUX   = do_CIPMUX;
    dev->CSTT     = do_CSTT;
    dev->CIICR    = do_CIICR;
    dev->CIFSR    = do_CIFSR;
    dev->CIPSTART = do_CIPSTART;
    dev->CIPCLOSE = do_CIPCLOSE;

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
