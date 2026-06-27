/**
 * message.h — 服务器协议层接口
 *
 * 在 net_man 之上实现与充电桩服务器的 TCP 文本协议通信。
 *
 * 协议格式 (纯文本, 基于行):
 *   - 每行以 \r\n 结尾
 *   - 空格分隔参数: "on 1 30 ABC123\r\n"
 *   - 客户端→服务器: PING / LOGIN / LOG / GETTIME / GETJFPG
 *   - 服务器→客户端: Logon / pong / on / off / time / over / reboot / ...
 *
 * 核心机制:
 *   - 命令表派发: argv[0] → 匹配命令名 → 调用处理函数
 *   - 心跳保活: 每 10s 发 PING, 30s 无数据 → 触发重连
 *   - 事件池: 固定数组池, 可靠重试上报 (如充电结束 over 需服务器 ACK)
 *
 * 分层关系:
 *   App 层             → msg_man.work()          (协议引擎)
 *   softpack/message   → air724->send_str()       (发送)
 *                      → air724->at->uart->read   (接收)
 *   softpack/net_man   → 状态机管理               (入网流程)
 *   softpack/air_man   → AT 命令封装              (已就绪后不使用)
 *
 * 使用方式:
 *   // Phase 3 就绪后:
 *   msg_man.init(air724);
 *   while (1) {
 *       msg_man.work();       // 驱动协议引擎
 *       if (g_msg_need_reconnect) {
 *           net_man->force_reset();
 *       }
 *   }
 */

#ifndef __MESSAGE_H__
#define __MESSAGE_H__

#include <stdint.h>
#include "air_man.h"

/* ---- 命令返回值 ---- */
#define MSG_RET_SUCCESS    1
#define MSG_RET_RECONNECT  (-9)   /* 需要重连 */
#define MSG_RET_ERROR      (-1)

/* ---- 事件类型 ---- */
enum {
    MSG_EVENT_NONE = 0,
    MSG_EVENT_CURR,          /* 电流数据上报 (即时) */
    MSG_EVENT_SOCK,          /* 插座事件 (需可靠确认, 如充电结束) */
    MSG_EVENT_NET,           /* 网络事件 */
    MSG_EVENT_MAINTENANCE,   /* 维护事件 (OTA等) */
};

/* ---- 事件结构体 ---- */
typedef struct {
    uint8_t  type;           /* 事件类型 (MSG_EVENT_xxx) */
    uint8_t  sock;           /* 插座编号 (0/1) */
    uint8_t  reserved[2];
    char     ddh[20];        /* 订单号 (用于服务器 over ACK 匹配) */
    uint32_t val;            /* 附加数值 */
    uint32_t time;           /* 时间 (分钟) */
} msg_event_t;

/* ---- 事件池条目 (固定数组, 非链表) ---- */
typedef struct {
    uint8_t     used;        /* 是否占用 */
    uint8_t     sta;         /* 0=准备发送, 1=等待间隔, 2=待删除 */
    uint32_t    dest_cnt;    /* 目标发送次数 */
    uint32_t    now_cnt;     /* 当前已发送次数 */
    uint32_t    wait_ms;     /* 重试间隔 (ms) */
    uint32_t    last_tick;   /* 上次发送时间戳 (tick ms) */
    msg_event_t event;       /* 事件数据 */
} pool_entry_t;

/* ---- 协议引擎接口 ---- */
typedef struct {
    /* 初始化协议引擎, 绑定到 4G 设备 */
    void (*init)(air_dev_pt dev);

    /* 主循环: 收数据 + 解析 + 派发 + 心跳 + 事件池 */
    void (*work)(void);

    /* 是否在线 (已收到 Logon) */
    int  (*is_online)(void);

    /* 发送一行文本到服务器 (自动追加 \r\n) */
    void (*send_line)(const char *line);

    /* 添加一个事件到重试池
     *   ev:       事件数据
     *   retry:    最大重试次数
     *   interval: 重试间隔 (ms) */
    int  (*add_event)(msg_event_t *ev, uint32_t retry, uint32_t interval);

    /* 服务器回复 over 时, 从池中删除对应事件 */
    void (*ack_event)(const char *ddh);

    /* 设置在线状态 (net_man 收到 Logon 后调用) */
    void (*set_online)(int online);

} msg_man_t, *msg_man_pt;

/* ---- 全局接口指针 ---- */
extern msg_man_pt msg_man;

/* ---- 重连信号 (由消息层设置, net_man 检查) ---- */
extern int g_msg_need_reconnect;

#endif /* __MESSAGE_H__ */
