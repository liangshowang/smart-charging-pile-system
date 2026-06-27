/**
 * net_man.c — 4G 网络初始化状态机实现
 *
 * 三阶段状态机驱动 4G 模块从冷启动到 TCP 连接就绪:
 *
 *   Phase 0 (RESET):  硬件复位 → 等 45s → AT 握手
 *   Phase 1 (INIT):   12 条 AT 命令序列 (CGMI→...→CIFSR)
 *   Phase 2 (CONNECT): CIPSTART → LOGIN → wait Logon
 *   Phase 3 (READY):   网络就绪, 状态机停在此处
 *
 * 容错机制:
 *   - 每条 AT 命令最多重试 3 次
 *   - 任意步骤失败 → goto __AIR_RST → 回到 Phase 0 重新开始
 *   - 重连计数器超过 5 次 → 打印错误 (后续 Step 25 改为看门狗复位)
 *
 * 调用方式:
 *   task_start 主循环反复调用 net_man->work(),
 *   每次调用阻塞执行一条 AT 命令 (~数百ms 到数秒)。
 *
 * 纯软件模块 — 不操作任何寄存器。
 */

#include "net_man.h"
#include "air_man.h"
#include "at_dev.h"
#include "message.h"
#include "drv_gpio.h"
#include "io_config.h"
#include "flashdb.h"
#include "sysprt.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdio.h>

/* ---- 单步最大重试次数 ---- */
#define CMD_MAX_ERR  3

/* ---- 错误宏: 记录行号并跳转到复位流程 ---- */
#define __ERR_AIR_RST()  do { line = __LINE__; goto __AIR_RST; } while(0)

/* ================================================================
 * 状态机内部状态
 * ================================================================ */
static int g_phase     = NET_PHASE_RESET;   /* 当前阶段 */
static int g_step      = 0;                 /* 当前步骤 */
static int g_reconnect = 0;                 /* 重连计数器 */

/* ================================================================
 * do_init — 初始化状态机
 *
 * 设备必须已由 task_start 通过 air_man->create_dev 创建。
 * 将状态机置为 RESET phase step 0。
 * ================================================================ */
static void do_init(void)
{
    g_phase     = NET_PHASE_RESET;
    g_step      = 0;
    g_reconnect = 0;

    sysprt->alog("[net_man] state machine initialized\r\n");
}

/* ================================================================
 * do_is_ready — 查询网络是否就绪
 * ================================================================ */
static int do_is_ready(void)
{
    return (g_phase == NET_PHASE_READY) ? 1 : 0;
}

/* ================================================================
 * do_get_state — 获取当前状态 (调试用)
 * ================================================================ */
static void do_get_state(int *phase, int *step, int *reconnect)
{
    if (phase)     *phase     = g_phase;
    if (step)      *step      = g_step;
    if (reconnect) *reconnect = g_reconnect;
}

/* ================================================================
 * do_force_reset — 强制回到复位阶段
 * ================================================================ */
static void do_force_reset(void)
{
    g_phase     = NET_PHASE_RESET;
    g_step      = 0;
    g_reconnect = 0;
    sysprt->alog("[net_man] forced reset\r\n");
}

/* ================================================================
 * RETRY_CMD — 带重试的 AT 命令执行宏
 *
 * expr:    会返回值的表达式 (通常是 air724->xxx(...))
 * max_err: 最大重试次数 (通常为 CMD_MAX_ERR)
 * dly_ms:  重试前等待毫秒数
 *
 * 成功 (ret==1) → 跳出循环, 继续下一步
 * 失败         → 延时后重试, 超过 max_err → 跳转到 __AIR_RST
 * ================================================================ */
#define RETRY_CMD(expr, max_err, dly_ms) do {        \
    err = 0;                                          \
    while (1) {                                       \
        ret = (expr);                                 \
        air_man->ptf_rxbuf(air724);                   \
        if (ret == 1) break;                          \
        sysprt->alog("[net_man] retry %d/%d, ret=%d\r\n", \
                     err + 1, max_err, ret);          \
        vTaskDelay(pdMS_TO_TICKS(dly_ms));             \
        if (++err >= (max_err)) {                      \
            line = __LINE__; goto __AIR_RST;           \
        }                                              \
    }                                                  \
} while(0)

/* ================================================================
 * do_work — 状态机主驱动函数
 *
 * 由 task_start 主循环反复调用。
 * 每次调用处理当前 phase/step 的一项工作:
 *   - 如果是 AT 命令 → 阻塞发送 + 等待响应 (数百ms~数十秒)
 *   - 如果是等待 → vTaskDelay (其他任务继续运行)
 *
 * 容错: 任何 AT 步骤失败 → 回到 Phase 0 重新开始
 * ================================================================ */
static void do_work(void)
{
    int err, ret, line = 0;

    if (air724 == NULL) {
        sysprt->aerr("[net_man] air724 not created!\r\n");
        return;
    }

    switch (g_phase) {

    /* ================================================================
     * Phase 0: RESET — 模块复位 + 等待启动
     * ================================================================ */
    case NET_PHASE_RESET:
        switch (g_step) {

        case 0: /* ---- 设备自检 ---- */
            sysprt->alog("\r\n[net_man] === Phase 0: RESET ===\r\n");
            sysprt->alog("[net_man] device check...\r\n");
            g_step++;
            break;

        case 1: /* ---- 硬件复位: NetRst 拉低→延时→拉高 ---- */
            sysprt->alog("[net_man] hardware reset (NetRst low→high)...\r\n");

            /* 拉低 NetRst 触发复位 */
            drv_gpio->write(PIN_NETRST, PIN_LOW);
            vTaskDelay(pdMS_TO_TICKS(1000));

            /* 拉高 NetRst 释放复位 */
            drv_gpio->write(PIN_NETRST, PIN_HIGH);
            sysprt->alog("[net_man] reset released, waiting module boot...\r\n");

            g_step++;
            break;

        case 2: /* ---- 等待模块启动 (45s) ---- */
            sysprt->alog("[net_man] waiting 45s for module to boot...\r\n");

            /* 每 5s 打印一个点, 共 45s */
            {
                int i;
                for (i = 0; i < 9; i++) {
                    vTaskDelay(pdMS_TO_TICKS(5000));
                    sysprt->alog(".");
                }
            }
            sysprt->alog("\r\n");

            g_step++;
            break;

        case 3: /* ---- AT 握手: 确认模块在线 ---- */
            sysprt->alog("[net_man] AT handshake...\r\n");
            RETRY_CMD(air724->AT(air724), 6, 2000);
            sysprt->alog("[net_man] module online, entering INIT phase\r\n");
            g_phase = NET_PHASE_INIT;
            g_step  = 0;
            break;

        default:
            g_step = 0;
            break;
        }
        break;

    /* ================================================================
     * Phase 1: INIT — AT 命令序列初始化网络
     *
     * 严格的线性序列, 每步必成, 失败则复位重来。
     * 顺序: CGMI → CGMR → CPIN → CSQ → CREG → CGATT
     *       → CIPMODE → CIPMUX → CSTT → CIICR → CIFSR
     * ================================================================ */
    case NET_PHASE_INIT:
        /* 每次进入 INIT 递增重连计数 */
        if (g_step == 0) {
            g_reconnect++;
            sysprt->alog("\r\n[net_man] === Phase 1: INIT (reconnect #%d) ===\r\n",
                         g_reconnect);

            /* 重连超过 5 次 → 打印告警 (后续 Step 25 触发看门狗复位) */
            if (g_reconnect > 5) {
                sysprt->aerr("[net_man] reconnect > 5, check SIM/module!\r\n");
            }
        }

        switch (g_step) {

        case 0: /* ---- CGMI: 查询制造商 ---- */
            sysprt->alog("[net_man] CGMI...\r\n");
            {
                char info[32];
                RETRY_CMD(air724->CGMI(air724, info, sizeof(info)),
                          CMD_MAX_ERR, 2000);
                sysprt->alog("[net_man] Manufacturer: %s\r\n", info);
            }
            g_step++;
            break;

        case 1: /* ---- CGMR: 查询固件版本 ---- */
            sysprt->alog("[net_man] CGMR...\r\n");
            {
                char ver[64];
                RETRY_CMD(air724->CGMR(air724, ver, sizeof(ver)),
                          CMD_MAX_ERR, 2000);
                sysprt->alog("[net_man] Firmware: %s\r\n", ver);
            }
            g_step++;
            break;

        case 2: /* ---- CPIN: 查询 SIM 卡 ---- */
            sysprt->alog("[net_man] CPIN...\r\n");
            {
                char sim_stat[16];
                RETRY_CMD(air724->CPIN(air724, sim_stat, sizeof(sim_stat)),
                          CMD_MAX_ERR, 2000);
                sysprt->alog("[net_man] SIM: %s\r\n", sim_stat);
            }
            g_step++;
            break;

        case 3: /* ---- CSQ: 查询信号质量 ---- */
            sysprt->alog("[net_man] CSQ...\r\n");
            {
                int rssi = 0, ber = 0;
                RETRY_CMD(air724->CSQ(air724, &rssi, &ber),
                          CMD_MAX_ERR, 2000);
                sysprt->alog("[net_man] Signal: rssi=%d, ber=%d\r\n", rssi, ber);
            }
            g_step++;
            break;

        case 4: /* ---- CREG: 查询网络注册 ---- */
            sysprt->alog("[net_man] CREG...\r\n");
            {
                int stat = 0;
                RETRY_CMD(air724->CREG(air724, &stat),
                          CMD_MAX_ERR, 3000);
                sysprt->alog("[net_man] Network reg: stat=%d\r\n", stat);
            }
            g_step++;
            break;

        case 5: /* ---- CGATT: 查询 GPRS 附着 ---- */
            sysprt->alog("[net_man] CGATT...\r\n");
            {
                int stat = 0;
                RETRY_CMD(air724->CGATT(air724, &stat),
                          CMD_MAX_ERR, 3000);
                sysprt->alog("[net_man] GPRS attach: stat=%d\r\n", stat);
            }
            g_step++;
            break;

        case 6: /* ---- CIPMODE=1: 设置透传模式 ---- */
            sysprt->alog("[net_man] CIPMODE=1...\r\n");
            RETRY_CMD(air724->CIPMODE(air724, 1),
                      CMD_MAX_ERR, 3000);
            sysprt->alog("[net_man] Transparent mode set\r\n");
            g_step++;
            break;

        case 7: /* ---- CIPMUX=0: 设置单连接 ---- */
            sysprt->alog("[net_man] CIPMUX=0...\r\n");
            RETRY_CMD(air724->CIPMUX(air724, 0),
                      CMD_MAX_ERR, 3000);
            sysprt->alog("[net_man] Single connection mode set\r\n");
            g_step++;
            break;

        case 8: /* ---- CSTT: 设置 APN (自动) ---- */
            sysprt->alog("[net_man] CSTT (auto APN)...\r\n");
            RETRY_CMD(air724->CSTT(air724, NULL, NULL, NULL),
                      CMD_MAX_ERR, 5000);
            sysprt->alog("[net_man] APN set\r\n");
            g_step++;
            break;

        case 9: /* ---- CIICR: 激活 GPRS (获取 IP, 耗时较长) ---- */
            sysprt->alog("[net_man] CIICR (activating GPRS, may take 30s)...\r\n");
            /* CIICR 只重试 1 次 (失败可能是网络问题, 快速复位重来) */
            RETRY_CMD(air724->CIICR(air724), 2, 3000);
            sysprt->alog("[net_man] GPRS activated\r\n");
            g_step++;
            break;

        case 10: /* ---- CIFSR: 查询 IP 地址 ---- */
            sysprt->alog("[net_man] CIFSR...\r\n");
            {
                char ip[32];
                RETRY_CMD(air724->CIFSR(air724, ip, sizeof(ip)),
                          CMD_MAX_ERR, 3000);
                sysprt->alog("[net_man] Local IP: %s\r\n", ip);
            }
            /* INIT 完成, 进入 CONNECT 阶段 */
            sysprt->alog("[net_man] INIT phase complete!\r\n");
            g_phase = NET_PHASE_CONNECT;
            g_step  = 0;
            break;

        default:
            g_step = 0;
            break;
        }
        break;

    /* ================================================================
     * Phase 2: CONNECT — TCP 连接 + LOGIN + 等待 Logon
     *
     * 服务器: www.armsoc.cn:9002
     * LOGIN 格式: "LOGIN <user_name> <user_pwd> vision\r\n"
     * ================================================================ */
    case NET_PHASE_CONNECT:
        if (g_step == 0) {
            sysprt->alog("\r\n[net_man] === Phase 2: CONNECT ===\r\n");
        }

        switch (g_step) {

        case 0: /* ---- AT 握手 (确保模块在线) ---- */
            sysprt->alog("[net_man] pre-connect AT handshake...\r\n");
            RETRY_CMD(air724->AT(air724), 3, 2000);
            g_step++;
            break;

        case 1: /* ---- CIPSTART: 连接服务器 ---- */
            sysprt->alog("[net_man] CIPSTART TCP://www.armsoc.cn:9002...\r\n");
            RETRY_CMD(air724->CIPSTART(air724, "TCP",
                                        "www.armsoc.cn", "9002"),
                      CMD_MAX_ERR, 3000);
            sysprt->alog("[net_man] TCP connected!\r\n");
            g_step++;
            break;

        case 2: /* ---- 发送 LOGIN ---- */
            {
                storage_t *st = flashdb->get();
                char login_buf[256];
                int len;

                sysprt->alog("[net_man] sending LOGIN...\r\n");

                /* 构造 LOGIN 消息: LOGIN <user> <pwd> vision */
                len = snprintf(login_buf, sizeof(login_buf),
                               "LOGIN %s %s vision",
                               st ? (char *)st->user_name : "test",
                               st ? (char *)st->user_pwd  : "123456");
                login_buf[sizeof(login_buf) - 1] = '\0';

                sysprt->alog("[net_man] LOGIN: %s (len=%d)\r\n", login_buf, len);

                /* 使用 send_str 发送 (不等 OK, 因为 LOGIN 是自定义协议) */
                air_man->clr_rxbuf(air724);
                air_man->send_str(air724, login_buf);

                sysprt->alog("[net_man] LOGIN sent, waiting Logon...\r\n");
            }
            g_step++;
            break;

        case 3: /* ---- 等待 Logon 响应 ---- */
            {
                /*
                 * 等待服务器返回 "Logon"。
                 * 轮询 UART 接收, 每次读 1 字节, 超时 10s。
                 *
                 * 注意: 这里直接操作 at_dev 的 uart 读取,
                 * 因为 CIPSTART 之后的连接是透明通道, 不走 send_cmd。
                 */
                uint32_t start_tick = xTaskGetTickCount();
                uint8_t *rxbuf = air724->rxbuf;
                uint32_t rxlen = 0;

                /* 清空缓冲 */
                air_man->clr_rxbuf(air724);

                sysprt->alog("[net_man] polling for Logon...\r\n");

                while (1) {
                    int n;

                    /* 读 1 字节 */
                    if (air724->at && air724->at->uart) {
                        n = air724->at->uart->read_rx_buf(
                            &rxbuf[rxlen], 1);
                    } else {
                        n = 0;
                    }

                    if (n > 0 && rxlen < air724->rxsize - 1) {
                        rxlen += n;
                        rxbuf[rxlen] = '\0';

                        /* 收到完整行 → 检查 Logon */
                        if (strstr((char *)rxbuf, "\r\n") != NULL) {
                            air724->rxlen = rxlen;
                            air_man->ptf_rxbuf(air724);

                            if (strstr((char *)rxbuf, "Logon") != NULL) {
                                sysprt->alog("[net_man] ***** LOGON SUCCESS! *****\r\n");
                                goto __LOGON_OK;
                            }

                            /* 不是 Logon → 清空继续等 */
                            memset(rxbuf, 0, air724->rxsize);
                            rxlen = 0;
                        }
                    }

                    /* 超时 15s */
                    uint32_t elapsed = (xTaskGetTickCount() - start_tick)
                                       * portTICK_PERIOD_MS;
                    if (elapsed > 15000) {
                        sysprt->alog("[net_man] Logon timeout (15s)\r\n");
                        line = __LINE__;
                        goto __AIR_RST;
                    }

                    vTaskDelay(pdMS_TO_TICKS(50));
                }

            __LOGON_OK:
                /* 网络就绪! 初始化协议层 */
                msg_man->init(air724);
                msg_man->set_online(1);

                g_phase = NET_PHASE_READY;
                g_step  = 0;
            }
            break;

        default:
            g_step = 0;
            break;
        }
        break;

    /* ================================================================
     * Phase 3: READY — 网络就绪, 空闲
     *
     * 状态机停在此处。后续 Step 22 将在此基础上建立
     * 服务器协议收发 (心跳/命令派发/事件上报)。
     * ================================================================ */
    case NET_PHASE_READY:
        /* ---- 协议引擎接管: 收数据 + 心跳 + 事件池 ---- */
        msg_man->work();

        /* 检查是否需要重连 (relogin/nologin/超时) */
        if (g_msg_need_reconnect) {
            sysprt->alog("[net_man] message layer requested reconnect\r\n");
            g_msg_need_reconnect = 0;
            g_phase = NET_PHASE_RESET;
            g_step  = 1;  /* 硬件复位 */
        } else {
            /* 让出 CPU, 避免忙等 */
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        break;

    default:
        g_phase = NET_PHASE_RESET;
        g_step  = 0;
        break;
    }

    return;

    /* ================================================================
     * __AIR_RST — 错误恢复: 回到 Phase 0 重新开始
     * ================================================================ */
__AIR_RST:
    sysprt->aerr("[net_man] ERROR at phase=%d step=%d line=%d → RESET\r\n",
                 g_phase, g_step, line);
    g_phase = NET_PHASE_RESET;
    g_step  = 1;  /* 从硬件复位开始 */
}

/* ================================================================
 * 静态实例 + 导出指针
 * ================================================================ */
static net_man_t m_net_man = {
    do_init,
    do_work,
    do_is_ready,
    do_get_state,
    do_force_reset,
};

net_man_pt net_man = &m_net_man;
