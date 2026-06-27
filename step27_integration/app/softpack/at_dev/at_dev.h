/**
 * at_dev.h — AT 命令引擎接口
 *
 * 通用 AT 命令框架, 通过 UART1 与 4G 模块 (AIR724) 通信。
 *
 * 设计原则:
 *   - 纯软件模块 (softpack), 不操作寄存器
 *   - 通过 drv_uart1 接口操作串口
 *   - 内部管理一个接收缓冲区, 用于累积 + 匹配响应
 *
 * 核心 API — send_cmd:
 *   int ret = at_dev->send_cmd(dev, "AT+CSQ", dto, cto);
 *
 *   参数:
 *     dev  — AT 设备指针
 *     cmd  — AT 命令 (不含 \r\n, 引擎自动追加)
 *     dto  — 数据超时 (ms): 超过 dto 未收到新数据, 判定本次接收结束
 *            0 或 0xFFFFFFFF = 不设数据超时 (逐字符慢速接收)
 *     cto  — 命令超时 (ms): 从发送开始算, 超过 cto 未得到结果
 *
 *   返回:
 *      1  — 成功 (收到 OK)
 *      0  — 失败 (收到 ERROR)
 *     -1  — 数据超时 (在 dto 内没有新字节)
 *     -2  — 命令超时 (超过 cto)
 *      3  — 收到透传提示符 '>'
 *
 * 自动波特率:
 *   at_dev->auto_baud(dev, cto):
 *     尝试常见波特率序列, 发送 "AT" 等待回显, 匹配则锁定。
 *
 * 使用示例:
 *   at_dev_pt dev = at_dev->create(uart1);
 *   at_dev->auto_baud(dev, 2000);
 *   at_dev->send_cmd(dev, "AT", 1000, 2000);       // 预期: OK
 *   at_dev->send_cmd(dev, "AT+CSQ", 100, 1000);    // 预期: +CSQ: xx,xx OK
 */

#ifndef __AT_DEV_H__
#define __AT_DEV_H__

#include <stdint.h>
#include "drv_uart1.h"

/* ---- AT 设备结构体 ---- */
typedef struct {
    drv_uart1_pt uart;         /* 绑定的 UART */
    uint8_t     *rxbuf;        /* 接收缓冲区 */
    uint32_t     rxsize;       /* 缓冲区大小 */
    uint32_t     rxlen;        /* 当前已接收字节数 */
} at_dev_t, *at_dev_pt;

/* ---- AT 设备管理器接口 ---- */
typedef struct {
    /* 创建 AT 设备 (绑定 UART, 分配接收缓冲) */
    at_dev_pt (*create)(drv_uart1_pt uart);

    /* 销毁 AT 设备 */
    void (*delete_dev)(at_dev_pt dev);

    /* 发送 AT 命令并等待响应
     *   cmd: AT 命令字符串 (不含 \r\n)
     *   dto: 数据超时 (ms), 0 = 不限制
     *   cto: 命令超时 (ms)
     *   返回: 1=OK, 0=ERROR, -1=数据超时, -2=命令超时, 3=透传提示符 */
    int (*send_cmd)(at_dev_pt dev, const char *cmd, uint32_t dto, uint32_t cto);

    /* 自动波特率匹配
     *   尝试 115200/57600/38400/19200/9600
     *   每个波特率发送 AT, 检查模块是否有回显
     *   返回: 0=成功, -1=失败 */
    int (*auto_baud)(at_dev_pt dev, uint32_t cto);

    /* 打印接收缓冲区内容 (调试用) */
    void (*dump_rx)(at_dev_pt dev);
} at_man_t, *at_man_pt;

/* ---- 全局接口指针 ---- */
extern at_man_pt at_man;

/* ---- 全局 4G 模块设备指针已上移至 air_man.h (air_dev_pt air724) ---- */

#endif /* __AT_DEV_H__ */
