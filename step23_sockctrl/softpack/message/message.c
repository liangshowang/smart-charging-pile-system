/**
 * message.c — 服务器协议层实现
 *
 * 实现充电桩与服务器的 TCP 文本协议通信:
 *   - 行解析 (\r\n 分割)
 *   - 命令行派发 (命令表匹配)
 *   - 心跳保活 (PING/PONG, 超时重连)
 *   - 事件池 (简单固定数组, 非链表)
 *
 * 协议格式:
 *   服务器→客户端每个命令一行, 空格分隔参数:
 *     Logon / pong / relogin / nologin / time YYYYMMDDhhmmss CK / on sock min ddh / off sock ddh
 *   客户端→服务器:
 *     PING 0 0 220 / LOGIN user pwd vision / GETTIME / LOG ...
 *
 * 纯软件模块 — 不操作任何寄存器。
 */

#include "message.h"
#include "air_man.h"
#include "at_dev.h"
#include "drv_uart1.h"
#include "sock_ctrl.h"
#include "sysprt.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ================================================================
 * 内部常量
 * ================================================================ */

#define LINEBUF_SIZE    1024    /* 行缓冲区大小 */
#define MAX_ARGC         16     /* 最大参数个数 */
#define MAX_POOL         10     /* 事件池最大条目数 */
#define PING_INTERVAL    10000  /* 心跳间隔 (ms) */
#define RECV_TIMEOUT     30000  /* 接收超时 (ms), 超时触发重连 */

/* ================================================================
 * 内部状态
 * ================================================================ */

static air_dev_pt g_dev;                /* 绑定的 4G 设备 */
static uint8_t    g_linebuf[LINEBUF_SIZE]; /* 行累积缓冲 */
static uint32_t   g_linelen;            /* 当前行长度 */

static int        g_logon_ok;           /* 已收到 Logon */
static uint32_t   g_last_ping_tick;     /* 上次 PING 时间 */
static uint32_t   g_last_recv_tick;     /* 上次收到数据时间 */

static pool_entry_t g_pool[MAX_POOL];   /* 事件池 (固定数组) */

/* ---- 重连信号 (外部可见) ---- */
int g_msg_need_reconnect = 0;

/* ================================================================
 * 字符串分割器 — 按空格切分 argc/argv
 *
 * 直接在 str 上修改 (写入 \0), 不分配新内存。
 * 返回 argc (参数个数)。
 * ================================================================ */
static int str_split(char *str, char **argv, int max_argc)
{
    int argc = 0;
    char *p = str;

    while (*p && argc < max_argc) {
        /* 跳过前导空白 */
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\r' || *p == '\n') break;

        /* 记录参数起始 */
        argv[argc++] = p;

        /* 找到参数结尾 */
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;

        /* 截断 */
        if (*p) {
            *p = '\0';
            p++;
        }
    }
    return argc;
}

/* ================================================================
 * 命令处理函数 (static, 通过命令表间接调用)
 * ================================================================ */

/* ---- Logon — 登录成功 ---- */
static int h_logon(int argc, char **argv)
{
    (void)argc; (void)argv;
    g_logon_ok = 1;
    sysprt->alog("[msg] Logon received — online!\r\n");
    return MSG_RET_SUCCESS;
}

/* ---- relogin — 服务器要求重新登录 ---- */
static int h_relogin(int argc, char **argv)
{
    (void)argc; (void)argv;
    sysprt->alog("[msg] relogin — reconnect required\r\n");
    g_msg_need_reconnect = 1;
    return MSG_RET_RECONNECT;
}

/* ---- nologin — 会话过期 ---- */
static int h_nologin(int argc, char **argv)
{
    (void)argc; (void)argv;
    sysprt->alog("[msg] nologin — session expired\r\n");
    g_msg_need_reconnect = 1;
    return MSG_RET_RECONNECT;
}

/* ---- pong — 心跳回复 ---- */
static int h_pong(int argc, char **argv)
{
    (void)argc; (void)argv;
    /* pong 收到说明链路正常, 不需要额外处理 */
    return MSG_RET_SUCCESS;
}

/* ---- over — 服务器确认充电结束事件
 *     格式: over <ddh> ---- */
static int h_over(int argc, char **argv)
{
    if (argc < 2) return MSG_RET_ERROR;

    sysprt->alog("[msg] over ddh=%s — ACK from server\r\n", argv[1]);

    /* 从事务池中删除对应事件 */
    msg_man->ack_event(argv[1]);

    return MSG_RET_SUCCESS;
}

/* ---- time — 时间同步
 *     格式: time YYYYMMDDhhmmss <checksum> ---- */
static int h_time(int argc, char **argv)
{
    if (argc < 2) return MSG_RET_ERROR;

    sysprt->alog("[msg] server time: %s\r\n", argv[1]);
    /*
     * TODO Step 24: dump_rtc_by_str(argv[1])
     * RTC 驱动和日历模块将在 Step 24 实现。
     */
    return MSG_RET_SUCCESS;
}

/* ---- jfpg — 分时电价时段
 *     格式: jfpg <时段1> <时段2> ... <checksum> ---- */
static int h_jfpg(int argc, char **argv)
{
    if (argc < 3) return MSG_RET_ERROR;

    sysprt->alog("[msg] jfpg: %d groups\r\n", argc - 2);
    /*
     * TODO Step 24: 解析时段字符串并存储到 time_slot 结构
     */
    return MSG_RET_SUCCESS;
}

/* ---- log — 服务器回复 log 相关 ---- */
static int h_log(int argc, char **argv)
{
    (void)argc; (void)argv;
    /* 服务器对客户端 LOG 上报的回复, 通常无需处理 */
    return MSG_RET_SUCCESS;
}

/* ---- PARAM — 参数上报回复 ---- */
static int h_PARAM(int argc, char **argv)
{
    (void)argc; (void)argv;
    return MSG_RET_SUCCESS;
}

/* ---- STATU — 状态上报回复 ---- */
static int h_STATU(int argc, char **argv)
{
    (void)argc; (void)argv;
    return MSG_RET_SUCCESS;
}

/* ---- setparam — 设置参数 ---- */
static int h_setparam(int argc, char **argv)
{
    (void)argc; (void)argv;
    sysprt->alog("[msg] setparam (TODO)\r\n");
    return MSG_RET_SUCCESS;
}

/* ---- getstatu — 查询状态 ---- */
static int h_getstatu(int argc, char **argv)
{
    (void)argc; (void)argv;
    sysprt->alog("[msg] getstatu (TODO)\r\n");
    return MSG_RET_SUCCESS;
}

/* ---- update — OTA 升级
 *     格式: update <url> <version> ... ---- */
static int h_update(int argc, char **argv)
{
    (void)argc; (void)argv;
    sysprt->alog("[msg] update (OTA) — TODO Step 26\r\n");
    /*
     * TODO Step 26: 触发 OTA 流程
     * sock_mq->put_event(ddh, _event_m_tenance, ...)
     */
    return MSG_RET_SUCCESS;
}

/* ---- on — 开始充电
 *     格式: on <sock> <minutes> <ddh> ---- */
static int h_on(int argc, char **argv)
{
    uint32_t sock, minutes;

    if (argc < 4) return MSG_RET_ERROR;

    sock    = atol(argv[1]);     /* 插座编号 (1-based) */
    minutes = atol(argv[2]);     /* 充电时长 (分钟) */

    /* 参数合法性检查 */
    if (sock < 1 || sock > 2) {
        sysprt->aerr("[msg] on: invalid sock=%lu\r\n", sock);
        return MSG_RET_ERROR;
    }
    if (minutes > 12 * 60) {
        minutes = 10 * 60;       /* 最大 10 小时 */
    }

    sysprt->alog("[msg] ON: sock=%lu, time=%lu min, ddh=%s\r\n",
                 sock, minutes, argv[3]);

    /* 转发给充电控制模块 (跨任务 FreeRTOS 队列) */
    {
        sock_order_t order;
        order.type    = SOCK_ORDER_START;
        order.sock    = (int)(sock - 1);  /* 1-based → 0-based */
        order.minutes = minutes;
        strncpy(order.ddh, argv[3], sizeof(order.ddh) - 1);
        order.ddh[sizeof(order.ddh) - 1] = '\0';

        if (sock_ctrl->send_order(&order) != 0) {
            sysprt->aerr("[msg] on: send_order failed\r\n");
        }
    }

    return MSG_RET_SUCCESS;
}

/* ---- off — 停止充电
 *     格式: off <sock> <ddh> ---- */
static int h_off(int argc, char **argv)
{
    uint32_t sock;

    if (argc < 3) return MSG_RET_ERROR;

    sock = atol(argv[1]);

    if (sock < 1 || sock > 2) {
        sysprt->aerr("[msg] off: invalid sock=%lu\r\n", sock);
        return MSG_RET_ERROR;
    }

    sysprt->alog("[msg] OFF: sock=%lu, ddh=%s\r\n", sock, argv[2]);

    /* 转发给充电控制模块 (跨任务 FreeRTOS 队列) */
    {
        sock_order_t order;
        order.type    = SOCK_ORDER_STOP;
        order.sock    = (int)(sock - 1);  /* 1-based → 0-based */
        order.minutes = 0;                /* STOP 不使用 */
        strncpy(order.ddh, argv[2], sizeof(order.ddh) - 1);
        order.ddh[sizeof(order.ddh) - 1] = '\0';

        if (sock_ctrl->send_order(&order) != 0) {
            sysprt->aerr("[msg] off: send_order failed\r\n");
        }
    }

    return MSG_RET_SUCCESS;
}

/* ---- reboot — 系统复位 ---- */
static int h_reboot(int argc, char **argv)
{
    (void)argc; (void)argv;
    sysprt->alog("[msg] REBOOT command received!\r\n");

    /* 延时 1s 让日志发送出去 */
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* 软件复位 */
    NVIC_SystemReset();
    return MSG_RET_SUCCESS;
}

/* ---- CLOSED — 模块通知连接关闭 ---- */
static int h_CLOSED(int argc, char **argv)
{
    (void)argc; (void)argv;
    sysprt->alog("[msg] CLOSED — connection lost\r\n");
    g_msg_need_reconnect = 1;
    return MSG_RET_RECONNECT;
}

/* ================================================================
 * 命令表 (双数组: 名字表 + 函数表, 索引一一对应)
 * ================================================================ */

/* 命令名字符串数组 */
static const char *g_cmd_names[] = {
    "Logon",      /*  0: 登录成功 */
    "relogin",    /*  1: 需要重新登录 */
    "nologin",    /*  2: 会话过期 */
    "over",       /*  3: 充电结束确认 */
    "log",        /*  4: 日志回复 */
    "pong",       /*  5: 心跳回复 */
    "PARAM",      /*  6: 参数回复 */
    "STATU",      /*  7: 状态回复 */
    "jfpg",       /*  8: 分时电价 */
    "time",       /*  9: 时间同步 */
    "setparam",   /* 10: 设置参数 */
    "getstatu",   /* 11: 查询状态 */
    "update",     /* 12: OTA 升级 */
    "on",         /* 13: 开始充电 */
    "off",        /* 14: 停止充电 */
    "reboot",     /* 15: 系统复位 */
    "CLOSED",     /* 16: 连接断开 */
};

/* 命令处理函数数组 */
typedef int (*cmd_handler_t)(int argc, char **argv);

static const cmd_handler_t g_cmd_handlers[] = {
    h_logon,      /*  0 */
    h_relogin,    /*  1 */
    h_nologin,    /*  2 */
    h_over,       /*  3 */
    h_log,        /*  4 */
    h_pong,       /*  5 */
    h_PARAM,      /*  6 */
    h_STATU,      /*  7 */
    h_jfpg,       /*  8 */
    h_time,       /*  9 */
    h_setparam,   /* 10 */
    h_getstatu,   /* 11 */
    h_update,     /* 12 */
    h_on,         /* 13 */
    h_off,        /* 14 */
    h_reboot,     /* 15 */
    h_CLOSED,     /* 16 */
};

#define CMD_COUNT  (sizeof(g_cmd_names) / sizeof(g_cmd_names[0]))

/* ================================================================
 * 命令派发 — 线性匹配 argv[0] 并调用对应处理函数
 * ================================================================ */
static int msg_dispatch(int argc, char **argv)
{
    uint32_t i;

    if (argc == 0 || argv[0] == NULL) return MSG_RET_ERROR;

    for (i = 0; i < CMD_COUNT; i++) {
        if (0 == strncmp(argv[0], g_cmd_names[i],
                         strlen(g_cmd_names[i]))) {
            return g_cmd_handlers[i](argc, argv);
        }
    }

    /* 未知命令 */
    sysprt->alog("[msg] unknown cmd: %s\r\n", argv[0]);
    return MSG_RET_ERROR;
}

/* ================================================================
 * 处理一行接收数据
 *
 * 1. 分割 argc/argv
 * 2. 命令派发
 * 3. 打印日志
 * ================================================================ */
static void msg_process_line(char *line)
{
    char *argv[MAX_ARGC];
    int argc;
    int ret;

    /* 跳过空行 */
    if (line[0] == '\0') return;

    argc = str_split(line, argv, MAX_ARGC);
    if (argc == 0) return;

    /* 打印接收内容 (调试) */
    {
        int j;
        sysprt->alog("[msg] recv:");
        for (j = 0; j < argc; j++) {
            printf(" [%s]", argv[j]);
        }
        printf("\r\n");
    }

    /* 派发 */
    ret = msg_dispatch(argc, argv);

    if (ret == MSG_RET_RECONNECT) {
        sysprt->alog("[msg] dispatch returned RECONNECT\r\n");
    }
}

/* ================================================================
 * 心跳 — 每 PING_INTERVAL ms 发送 PING
 * ================================================================ */
static void msg_ping(void)
{
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    if ((now - g_last_ping_tick) >= PING_INTERVAL) {
        g_last_ping_tick = now;
        msg_man->send_line("PING 0 0 220");
    }
}

/* ================================================================
 * 接收超时检测 — 超过 RECV_TIMEOUT 无数据 → 触发重连
 * ================================================================ */
static void msg_check_timeout(void)
{
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    if (g_logon_ok && (now - g_last_recv_tick) >= RECV_TIMEOUT) {
        sysprt->aerr("[msg] recv timeout (%lums), reconnect!\r\n",
                     now - g_last_recv_tick);
        g_msg_need_reconnect = 1;
        g_logon_ok = 0;
    }
}

/* ================================================================
 * 事件池操作 (固定数组, 非链表)
 * ================================================================ */

/* ---- 事件池初始化 ---- */
static void pool_init(void)
{
    memset(g_pool, 0, sizeof(g_pool));
}

/* ---- 查找空闲条目 ---- */
static pool_entry_t *pool_alloc(void)
{
    int i;
    for (i = 0; i < MAX_POOL; i++) {
        if (!g_pool[i].used) {
            g_pool[i].used = 1;
            return &g_pool[i];
        }
    }
    return NULL;
}

/* ---- 添加事件到池 ---- */
static int do_add_event(msg_event_t *ev, uint32_t retry, uint32_t interval)
{
    pool_entry_t *entry = pool_alloc();
    if (entry == NULL) {
        sysprt->aerr("[msg] pool full!\r\n");
        return -1;
    }

    entry->sta       = 0;         /* 准备发送 */
    entry->dest_cnt  = retry;
    entry->now_cnt   = 0;
    entry->wait_ms   = interval;
    entry->last_tick = xTaskGetTickCount() * portTICK_PERIOD_MS;
    memcpy(&entry->event, ev, sizeof(msg_event_t));

    sysprt->alog("[msg] pool add: ddh=%s, retry=%lu\r\n",
                 ev->ddh, retry);
    return 0;
}

/* ---- 确认事件 (匹配 ddh) ---- */
static void do_ack_event(const char *ddh)
{
    int i;
    for (i = 0; i < MAX_POOL; i++) {
        if (g_pool[i].used &&
            0 == strcmp(g_pool[i].event.ddh, ddh)) {
            sysprt->alog("[msg] pool ack: ddh=%s → removed\r\n", ddh);
            memset(&g_pool[i], 0, sizeof(pool_entry_t));
            return;
        }
    }
}

/* ---- 事件池工作: 遍历, 发送/等待/删除 ---- */
static void pool_work(void)
{
    int i;
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    for (i = 0; i < MAX_POOL; i++) {
        pool_entry_t *e = &g_pool[i];
        if (!e->used) continue;

        switch (e->sta) {

        case 0: /* 准备发送 — 发送事件数据 */
            switch (e->event.type) {
            case MSG_EVENT_SOCK:
                /* 充电结束事件: LOG over <ddh> <...> */
                {
                    char buf[128];
                    snprintf(buf, sizeof(buf),
                             "LOG over %s 0 0 0 0 0 0 0 0",
                             e->event.ddh);
                    msg_man->send_line(buf);
                }
                break;

            case MSG_EVENT_NET:
                /* 网络事件: LOG <...> */
                {
                    char buf[128];
                    snprintf(buf, sizeof(buf),
                             "LOG 14 01 00 %s", e->event.ddh);
                    msg_man->send_line(buf);
                }
                break;

            default:
                break;
            }

            e->now_cnt++;
            if (e->now_cnt < e->dest_cnt) {
                e->sta = 1;       /* 进入等待 */
                e->last_tick = now;
            } else {
                e->sta = 2;       /* 超过次数, 待删除 */
                sysprt->alog("[msg] pool ddh=%s reached max retry\r\n",
                             e->event.ddh);
            }
            break;

        case 1: /* 等待间隔 */
            if ((now - e->last_tick) >= e->wait_ms) {
                e->sta = 0;       /* 回到发送状态 */
            }
            break;

        case 2: /* 待删除 */
            sysprt->alog("[msg] pool delete: ddh=%s\r\n", e->event.ddh);
            memset(e, 0, sizeof(pool_entry_t));
            break;

        default:
            break;
        }
    }
}

/* ================================================================
 * do_init — 初始化协议引擎
 * ================================================================ */
static void do_init(air_dev_pt dev)
{
    g_dev       = dev;
    g_linelen   = 0;
    g_logon_ok  = 0;
    g_msg_need_reconnect = 0;
    g_last_ping_tick = 0;
    g_last_recv_tick = xTaskGetTickCount() * portTICK_PERIOD_MS;

    memset(g_linebuf, 0, sizeof(g_linebuf));
    pool_init();

    sysprt->alog("[msg] protocol engine initialized\r\n");
}

/* ================================================================
 * do_work — 协议引擎主循环
 *
 * 1. 从 UART 读取 TCP 数据, 累积到行缓冲
 * 2. 遇到 \r\n → 处理完整行
 * 3. 心跳检查
 * 4. 超时检查
 * 5. 事件池处理
 *
 * 应在 task_start 主循环中反复调用。
 * ================================================================ */
static void do_work(void)
{
    int rd_len;
    uint32_t now;

    if (g_dev == NULL) return;

    now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    /* ---- 1. 从 UART 读取数据 ---- */
    while (g_linelen < LINEBUF_SIZE - 1) {
        rd_len = g_dev->at->uart->read_rx_buf(
            &g_linebuf[g_linelen], 1);
        if (rd_len <= 0) break;

        g_last_recv_tick = now;  /* 有数据 → 重置超时 */

        /* 检查是否有完整行 (\r\n) */
        if (g_linelen > 0 &&
            g_linebuf[g_linelen - 1] == '\r' &&
            g_linebuf[g_linelen] == '\n') {

            /* 截掉 \r\n, 转为 C 字符串 */
            g_linebuf[g_linelen - 1] = '\0';
            g_linebuf[g_linelen]     = '\0';

            /* 处理这一行 */
            msg_process_line((char *)g_linebuf);

            /* 清空行缓冲 */
            g_linelen = 0;
            memset(g_linebuf, 0, LINEBUF_SIZE);
        } else {
            g_linelen++;
        }
    }

    /* 行缓冲溢出保护 */
    if (g_linelen >= LINEBUF_SIZE - 1) {
        sysprt->aerr("[msg] linebuf overflow!\r\n");
        g_linelen = 0;
        memset(g_linebuf, 0, LINEBUF_SIZE);
    }

    /* ---- 2. 心跳 ---- */
    if (g_logon_ok) {
        msg_ping();
    }

    /* ---- 3. 接收超时检测 ---- */
    msg_check_timeout();

    /* ---- 4. 事件池处理 ---- */
    pool_work();
}

/* ---- is_online ---- */
static int do_is_online(void)
{
    return g_logon_ok;
}

/* ---- send_line — 发送一行 (自动追加 \r\n) ---- */
static void do_send_line(const char *line)
{
    if (g_dev == NULL) return;

    air_man->send_str(g_dev, line);

    /* 调试: 打印发送内容 */
    sysprt->alog("[msg] send: %s\r\n", line);
}

/* ---- set_online — 设置在线状态 (net_man 收到 Logon 后调用) ---- */
static void do_set_online(int online)
{
    g_logon_ok = online;
    if (online) {
        g_last_recv_tick = xTaskGetTickCount() * portTICK_PERIOD_MS;
        g_msg_need_reconnect = 0;
        sysprt->alog("[msg] set online\r\n");
    }
}

/* ================================================================
 * 静态实例 + 导出指针
 * ================================================================ */
static msg_man_t m_msg_man = {
    do_init,
    do_work,
    do_is_online,
    do_send_line,
    do_add_event,
    do_ack_event,
    do_set_online,
};

msg_man_pt msg_man = &m_msg_man;
