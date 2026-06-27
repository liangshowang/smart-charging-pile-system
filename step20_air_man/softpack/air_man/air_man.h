/**
 * air_man.h — 4G 模块管理层接口
 *
 * 在 at_dev（通用 AT 引擎）之上封装 AIR724 的设备管理层。
 *
 * 职责:
 *   - 创建/销毁 4G 模块设备实例
 *   - 提供原始字符串发送 (send_str, 不等响应)
 *   - 管理接收缓冲区 (clr_rxbuf / ptf_rxbuf)
 *   - 提供常用 AT 命令的便捷封装 (内部调用 at_dev 做 OK/ERROR 判定)
 *
 * 分层关系:
 *   App 层          → air_man->xxx()        (发具体 AT 命令)
 *   softpack/air_man → at_man->send_cmd()   (通用 AT 引擎)
 *   softpack/at_dev  → uart1->send/read     (UART 收发)
 *   drv/drv_uart1    → drvp_uart1           (硬件操作)
 *
 * 使用示例:
 *   air724 = air_man->create_dev(uart1);
 *   air_man->AT(air724);                    // AT → OK
 *   air_man->CSQ(air724, &rssi, &ber);      // AT+CSQ → 解析信号值
 *   air_man->send_str(air724, "AT\r\n");    // 裸发送
 */

#ifndef __AIR_MAN_H__
#define __AIR_MAN_H__

#include <stdint.h>
#include "at_dev.h"

/* ---- 4G 模块设备结构体 ---- */
typedef struct {
    at_dev_pt   at;           /* 底层 AT 引擎 */

    uint8_t    *rxbuf;        /* 接收缓冲区 (拷贝自 at_dev 响应) */
    uint32_t    rxlen;        /* 已接收字节数 */
    uint32_t    rxsize;       /* 缓冲区大小 */

    /* ---- AT 命令便捷封装 (函数指针, 可被具体模块替换) ---- */

    /* AT — 基础联通性测试 */
    int (*AT)(void *dev);

    /* AT+CPIN? — 查询 SIM 卡状态, result 返回 "READY" 等 */
    int (*CPIN)(void *dev, char *result, int maxlen);

    /* AT+CSQ — 查询信号质量, rssi=0~31, ber=0~7 */
    int (*CSQ)(void *dev, int *rssi, int *ber);

    /* AT+CREG? — 查询网络注册状态, stat=0未注册/1已注册/5漫游 */
    int (*CREG)(void *dev, int *stat);

    /* AT+CGATT? — 查询 GPRS 附着状态, stat=0分离/1附着 */
    int (*CGATT)(void *dev, int *stat);

} air_dev_t, *air_dev_pt;


/* ---- 4G 模块管理器接口 ---- */
typedef struct {
    /* 创建 4G 模块设备 (绑定到 UART) */
    air_dev_pt (*create_dev)(drv_uart1_pt uart);

    /* 销毁设备 */
    void (*delete_dev)(air_dev_pt dev);

    /* 原始字符串发送 (追加 \r\n, 不等待响应) */
    void (*send_str)(air_dev_pt dev, const char *str);

    /* 清空接收缓冲区 (清 UART 环形缓冲 + 设备缓冲) */
    void (*clr_rxbuf)(air_dev_pt dev);

    /* 打印接收缓冲区 (调试用) */
    void (*ptf_rxbuf)(air_dev_pt dev);

} air_man_t, *air_man_pt;

/* ---- 全局接口指针 ---- */
extern air_man_pt air_man;

/* ---- 全局 4G 模块设备指针 ---- */
extern air_dev_pt air724;

#endif /* __AIR_MAN_H__ */
