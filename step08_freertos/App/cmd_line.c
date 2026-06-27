/**
 * cmd_line.c — 交互式命令行
 *
 * 两态状态机:
 *   状态0: 打印提示符 ">"
 *   状态1: 逐字节读取, 处理回显/退格/TAB/回车, 匹配命令表执行
 *
 * 命令表驱动: 新增命令只需在 cmd_list[] 加一行
 */

#include "SWM320.h"
#include "loopbuf.h"
#include "drv_uart0.h"
#include "cmd_line.h"
#include <stdio.h>
#include <string.h>
#include <io_config.h>
#include "FreeRTOS.h"
#include "task.h"

/* ================================================================
 * 工具宏
 * ================================================================ */
#define ITEM_NUM(items)  (sizeof(items) / sizeof((items)[0]))

#define MAX_ARGC   10
#define CMD_BUF_LEN 128

/* ================================================================
 * 类型定义
 * ================================================================ */
typedef int (*deal_cmd_t)(int argc, char **argv);

typedef struct {
    const char *name;
    deal_cmd_t  entry;
} cmd_t;

/* ================================================================
 * 命令声明
 * ================================================================ */
static int cmd_test(int argc, char **argv);
static int cmd_clear(int argc, char **argv);
static int cmd_help(int argc, char **argv);
static int cmd_reboot(int argc, char **argv);
static int cmd_led(int argc, char **argv);
static int cmd_relay(int argc, char **argv);
static int cmd_io(int argc, char **argv);
static int cmd_info(int argc, char **argv);

/* ================================================================
 * 命令表
 * ================================================================ */
static const cmd_t cmd_list[] = {
    { "test",   cmd_test   },
    { "clear",  cmd_clear  },
    { "help",   cmd_help   },
    { "reboot", cmd_reboot },
    { "led",    cmd_led    },
    { "relay",  cmd_relay  },
    { "io",     cmd_io     },
    { "info",   cmd_info   },
};

/* ================================================================
 * 命令行状态
 * ================================================================ */
static uint8_t cmd_buf[CMD_BUF_LEN];
static int     cmd_len = 0;
static int     sta     = 0;

/* ================================================================
 * split_string_n — 分割字符串
 *
 * 用 strchr 逐字符扫描, 不依赖 strtok_r, 纯 C89 兼容。
 * 返回 token 个数, 最多 max 个。
 * ================================================================ */
static int split_string_n(int max, char *str, char **result,
                          const char *delimiters)
{
    int   count = 0;
    char *p     = str;

    /* 跳过前导分隔符 */
    while (*p && strchr(delimiters, *p))
        p++;

    while (*p && count < max) {
        result[count++] = p;               /* 记录 token 起始 */

        /* 找到 token 结尾 */
        while (*p && !strchr(delimiters, *p))
            p++;

        if (*p) {
            *p = '\0';                     /* 截断 token */
            p++;
        }

        /* 跳过 token 之间的分隔符 */
        while (*p && strchr(delimiters, *p))
            p++;
    }

    return count;
}

/* ================================================================
 * 命令实现
 * ================================================================ */

/* test — 回显所有参数, 用于验证参数分割 */
static int cmd_test(int argc, char **argv)
{
    int i;

    printf("%s\r\n", __func__);
    for (i = 0; i < argc; i++) {
        printf("  argv[%d]: %s\r\n", i, argv[i]);
    }
    return 0;
}

/* clear — 清屏 (打印空行) */
static int cmd_clear(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("\r\n\r\n\r\n\r\n\r\n");
    return 0;
}

/* help — 列出所有可用命令 */
static int cmd_help(int argc, char **argv)
{
    uint32_t i;

    (void)argc;
    (void)argv;

    printf("\r\n=== Commands ===\r\n");
    for (i = 0; i < ITEM_NUM(cmd_list); i++) {
        printf("  %s\r\n", cmd_list[i].name);
    }
    printf("\r\n");
    return 0;
}

/* reboot — 软件复位 */
static int cmd_reboot(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("Rebooting...\r\n");
    NVIC_SystemReset();
    return 0;
}

/* led on|off — 控制板载状态 LED */
static int cmd_led(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: led on|off\r\n");
        return -1;
    }

    if (strcmp(argv[1], "on") == 0) {
        GPIO_SetBit(GPIOC, PIN4);
        printf("LED ON\r\n");
    } else if (strcmp(argv[1], "off") == 0) {
        GPIO_ClrBit(GPIOC, PIN4);
        printf("LED OFF\r\n");
    } else {
        printf("Invalid: %s (expect on|off)\r\n", argv[1]);
        return -1;
    }
    return 0;
}

/* relay <0|1> on|off — 控制继电器 */
static int cmd_relay(int argc, char **argv)
{
    GPIO_TypeDef *gpio;
    uint32_t      pin;

    if (argc < 3) {
        printf("Usage: relay 0|1 on|off\r\n");
        return -1;
    }

    /* 解析继电器编号 */
    if (strcmp(argv[1], "0") == 0) {
        gpio = GPIOM;
        pin  = PIN2;
    } else if (strcmp(argv[1], "1") == 0) {
        gpio = GPIOB;
        pin  = PIN12;
    } else {
        printf("Invalid relay: %s (expect 0 or 1)\r\n", argv[1]);
        return -1;
    }

    /* 解析动作 */
    if (strcmp(argv[2], "on") == 0) {
        GPIO_SetBit(gpio, pin);
        printf("ELC%d ON\r\n", (gpio == GPIOM) ? 0 : 1);
    } else if (strcmp(argv[2], "off") == 0) {
        GPIO_ClrBit(gpio, pin);
        printf("ELC%d OFF\r\n", (gpio == GPIOM) ? 0 : 1);
    } else {
        printf("Invalid: %s (expect on|off)\r\n", argv[2]);
        return -1;
    }
    return 0;
}

/* io — 打印所有 IO 引脚状态 */
static int cmd_io(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    io_print_all();
    return 0;
}

/* info — 打印系统信息 */
static int cmd_info(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("\r\n=== System Info ===\r\n");
    printf("  CPU Clock : %d Hz\r\n", SystemCoreClock);
    printf("  Tick      : %d ms\r\n", (int)xTaskGetTickCount());
    printf("  UART      : 115200-8-N-1\r\n");
    printf("\r\n");
    return 0;
}

/* ================================================================
 * cmd_line_work — 命令行主循环 (main 中轮询调用)
 * ================================================================ */
void cmd_line_work(void)
{
    uint8_t byte;
    int     argc, i;
    char   *argv[MAX_ARGC];
    char    copy[CMD_BUF_LEN];   /* 拷贝一份给 split 破坏 */

    switch (sta)
    {
    /* ---- 状态0: 打印提示符 ---- */
    case 0:
        printf("\r\n>");
        memset(cmd_buf, 0, sizeof(cmd_buf));
        cmd_len = 0;
        sta = 1;
        break;

    /* ---- 状态1: 等待输入 ---- */
    case 1:
        /* 从环形缓冲读一个字节 */
        if (loopbuf_read(&g_uart0_rxbuf, &byte) == 0)
            break;

        /* 缓冲区溢出保护 */
        if (cmd_len >= CMD_BUF_LEN) {
            sta = 0;
            break;
        }

        switch (byte)
        {
        /* TAB — 暂不处理 (可扩展自动补全) */
        case 0x09:
            break;

        /* Backspace / DEL */
        case 0x7F:
        case '\b':
            if (cmd_len > 0) {
                cmd_len--;
                cmd_buf[cmd_len] = '\0';
            }
            /* 重新打印当前行 */
            printf("\r\n>%s", cmd_buf);
            break;

        /* LF — 某些工具发送 \n, 忽略掉 (等 \r) */
        case '\n':
            if (cmd_len == 0)
                sta = 0;   /* 空行 + \n 直接重新提示 */
            break;

        /* CR — 回车, 执行命令 */
        case '\r':
            cmd_buf[cmd_len] = '\0';

            if (cmd_len == 0) {
                sta = 0;   /* 空行, 重新提示 */
                break;
            }

            /* 拷贝一份, split_string_n 会破坏原串 */
            memcpy(copy, cmd_buf, cmd_len + 1);

            argc = split_string_n(MAX_ARGC, copy, argv, " \t\n\r");

            if (argc == 0) {
                sta = 0;
                break;
            }

            /* 匹配命令表并执行 */
            {
                int found = 0;
                for (i = 0; i < (int)ITEM_NUM(cmd_list); i++) {
                    if (strcmp(argv[0], cmd_list[i].name) == 0) {
                        printf("\r\n");
                        cmd_list[i].entry(argc, argv);
                        found = 1;
                        sta = 0;
                        break;
                    }
                }

                if (!found) {
                    printf("\r\n  unknown command: %s\r\n", argv[0]);
                    sta = 0;
                }
            }
            break;

        /* 普通字符 — 回显并存入缓冲 */
        default:
            cmd_buf[cmd_len++] = byte;
            printf("%c", byte);
            break;
        }
        break;

    default:
        sta = 0;
        break;
    }
}
