/**
 * cmd_line.c — 交互式命令行
 *
 * 两态状态机:
 *   状态0: 打印提示符 ">"
 *   状态1: 逐字节读取, 处理回显/退格/TAB/回车, 匹配命令表执行
 *
 * 命令表驱动: 新增命令只需在 cmd_list[] 加一行。
 *
 * 依赖:
 *   drv_uart0  — uart0->read_rx_buf / uart0->send  (UART 双缓冲接口)
 *   drv_gpio   — GPIO 驱动 (drv_gpio->write)
 *   FreeRTOS   — xTaskGetTickCount()
 */

/* ---- 系统头文件 ---- */
#include <stdio.h>
#include <string.h>

/* ---- SDK ---- */
#include "SWM320.h"

/* ---- FreeRTOS ---- */
#include "FreeRTOS.h"
#include "task.h"

/* ---- 驱动层 ---- */
#include "drv_uart0.h"
#include "drv_gpio.h"

/* ---- 应用层 ---- */
#include "io_config.h"
#include "cmd_line.h"
#include "flash_partition.h"
#include "SWM320_flash.h"
#include "drv_wdt.h"
#include "drv_buzzer.h"
#include "drv_adc.h"

/* ---- 引脚编号定义在 io_config.h 中 ---- */
/* PIN_ELC0 / PIN_ELC1 / PIN_SLED — SWM320RET7 物理封装号 */

/* ================================================================
 * 工具宏
 * ================================================================ */
#define ITEM_NUM(items)  (sizeof(items) / sizeof((items)[0]))
#define MAX_ARGC         10
#define CMD_BUF_LEN      128

/* ================================================================
 * 类型定义
 * ================================================================ */
typedef int (*deal_cmd_t)(int argc, char **argv);

typedef struct {
    const char *name;
    deal_cmd_t  entry;
} cmd_t;

/* ================================================================
 * 命令声明 (前置, 因为 cmd_list[] 先于函数体)
 * ================================================================ */
static int cmd_test  (int argc, char **argv);
static int cmd_clear (int argc, char **argv);
static int cmd_help  (int argc, char **argv);
static int cmd_reboot(int argc, char **argv);
static int cmd_led   (int argc, char **argv);
static int cmd_relay (int argc, char **argv);
static int cmd_io    (int argc, char **argv);
static int cmd_info       (int argc, char **argv);
static int cmd_reboot_ota (int argc, char **argv);
static int cmd_wdt        (int argc, char **argv);
static int cmd_buzz       (int argc, char **argv);
static int cmd_adc        (int argc, char **argv);

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
    { "ota",    cmd_reboot_ota },
    { "wdt",    cmd_wdt    },
    { "buzz",   cmd_buzz   },
    { "adc",    cmd_adc    },
};

/* ================================================================
 * 命令行状态 (模块级全局变量)
 * ================================================================ */
static uint8_t cmd_buf[CMD_BUF_LEN];
static int     cmd_len;
static int     sta;

/* ================================================================
 * split_string_n — 分割字符串
 *
 * 在原串上把分隔符替换为 '\0', 返回 token 个数 (最多 max)。
 * 不依赖 strtok_r, 纯 C89 strchr 实现。
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
        result[count++] = p;

        /* 找 token 结尾 */
        while (*p && !strchr(delimiters, *p))
            p++;

        if (*p) {
            *p = '\0';                     /* 截断 */
            p++;
        }

        /* 跳过 token 间分隔符 */
        while (*p && strchr(delimiters, *p))
            p++;
    }

    return count;
}

/* ================================================================
 * 命令实现
 * ================================================================ */

static int cmd_test(int argc, char **argv)
{
    int i;

    printf("%s\r\n", __func__);
    for (i = 0; i < argc; i++)
        printf("  argv[%d]: %s\r\n", i, argv[i]);
    return 0;
}

static int cmd_clear(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("\r\n\r\n\r\n\r\n\r\n");
    return 0;
}

static int cmd_help(int argc, char **argv)
{
    uint32_t i;

    (void)argc; (void)argv;

    printf("\r\n=== Commands ===\r\n");
    for (i = 0; i < ITEM_NUM(cmd_list); i++)
        printf("  %s\r\n", cmd_list[i].name);
    printf("\r\n");
    return 0;
}

static int cmd_reboot(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("Rebooting...\r\n");
    NVIC_SystemReset();
    return 0;
}

/* ================================================================
 * cmd_reboot_ota — 设置 OTA 模式并重启
 *
 * 写入 boot_app = _APP_UPDATE 到 CONF 分区,
 * 然后复位 MCU。复位后 BL 检测到 _APP_UPDATE,
 * 进入 OTA 模式下载固件。
 * ================================================================ */
static int cmd_reboot_ota(int argc, char **argv)
{
    boot_config_t cfg;

    (void)argc; (void)argv;

    printf("Setting OTA mode and rebooting...\r\n");

    Flash_Param_at_xMHz(120);

    /* 读当前配置 (如果未初始化则用默认值) */
    if (BOOT_CFG_PTR->boot_code == BOOT_CODE_MAGIC) {
        cfg.boot_code = BOOT_CFG_PTR->boot_code;
        cfg.boot_app  = _APP_UPDATE;
        cfg.ota_req   = 1;
        cfg.net_mode  = BOOT_CFG_PTR->net_mode;
    } else {
        cfg.boot_code = BOOT_CODE_MAGIC;
        cfg.boot_app  = _APP_UPDATE;
        cfg.ota_req   = 1;
        cfg.net_mode  = 1;  /* 默认 Air724 */
    }

    FLASH_Erase(PART_CONF_START);
    FLASH_Write(PART_CONF_START, (uint32_t *)&cfg,
                (sizeof(cfg) + 3) / 4);

    printf("boot_app = _APP_UPDATE, rebooting in 500ms...\r\n");

    /* 等 500ms (让串口发完) */
    {
        volatile uint32_t d;
        for (d = 0; d < 50000000; d++) __NOP();
    }

    NVIC_SystemReset();
    return 0;
}

static int cmd_led(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: led on|off\r\n");
        return -1;
    }

    if (strcmp(argv[1], "on") == 0) {
        drv_gpio->write(PIN_SLED, PIN_HIGH);
        printf("LED ON\r\n");
    } else if (strcmp(argv[1], "off") == 0) {
        drv_gpio->write(PIN_SLED, PIN_LOW);
        printf("LED OFF\r\n");
    } else {
        printf("Invalid: %s (expect on|off)\r\n", argv[1]);
        return -1;
    }
    return 0;
}

static int cmd_relay(int argc, char **argv)
{
    int         rly_pin;
    const char *rly_name;

    if (argc < 3) {
        printf("Usage: relay 0|1 on|off\r\n");
        return -1;
    }

    /* 解析继电器编号 → 物理引脚 */
    if (strcmp(argv[1], "0") == 0) {
        rly_pin  = PIN_ELC0;
        rly_name = "ELC0";
    } else if (strcmp(argv[1], "1") == 0) {
        rly_pin  = PIN_ELC1;
        rly_name = "ELC1";
    } else {
        printf("Invalid relay: %s (expect 0 or 1)\r\n", argv[1]);
        return -1;
    }

    /* 解析动作 */
    if (strcmp(argv[2], "on") == 0) {
        drv_gpio->write(rly_pin, PIN_HIGH);
        printf("%s ON\r\n", rly_name);
    } else if (strcmp(argv[2], "off") == 0) {
        drv_gpio->write(rly_pin, PIN_LOW);
        printf("%s OFF\r\n", rly_name);
    } else {
        printf("Invalid: %s (expect on|off)\r\n", argv[2]);
        return -1;
    }
    return 0;
}

static int cmd_io(int argc, char **argv)
{
    (void)argc; (void)argv;
    io_print_all();
    return 0;
}

static int cmd_info(int argc, char **argv)
{
    (void)argc; (void)argv;

    printf("\r\n=== System Info ===\r\n");
    printf("  CPU Clock : %d Hz\r\n", SystemCoreClock);
    printf("  Tick      : %d ms\r\n", (int)xTaskGetTickCount());
    printf("  UART      : 115200-8-N-1\r\n");
    printf("\r\n");
    return 0;
}

/* ================================================================
 * cmd_wdt — WDT 状态查询 / 喂狗 / 禁用
 *   wdt          — 查询当前计数值
 *   wdt feed     — 手动喂狗
 *   wdt test     — 停止喂狗, 等复位 (验证 WDT 功能)
 * ================================================================ */
static int cmd_wdt(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "feed") == 0) {
        wdt->feed();
        printf("WDT: feed OK, value=%lu\r\n", wdt->get_value());
    } else if (argc >= 2 && strcmp(argv[1], "test") == 0) {
        printf("WDT: will reset in ~5s... (no more feed)\r\n");
        /* 不再喂狗, WDT 将复位 MCU */
    } else if (argc >= 2 && strcmp(argv[1], "off") == 0) {
        wdt->disable();
        printf("WDT: disabled (debug only!)\r\n");
    } else {
        printf("WDT: value=%lu (5s timeout, in reset mode)\r\n",
               wdt->get_value());
        printf("Usage: wdt [feed|test|off]\r\n");
    }
    return 0;
}

/* ================================================================
 * cmd_buzz — 蜂鸣器测试
 *   buzz short / double / long / alarm / charging / finish / error / off
 * ================================================================ */
static int cmd_buzz(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: buzz short|double|long|alarm|charging|finish|error|off\r\n");
        return -1;
    }

    if      (strcmp(argv[1], "short")    == 0) buzz->beep(BUZZ_BEEP_SHORT);
    else if (strcmp(argv[1], "double")   == 0) buzz->beep(BUZZ_BEEP_DOUBLE);
    else if (strcmp(argv[1], "long")     == 0) buzz->beep(BUZZ_BEEP_LONG);
    else if (strcmp(argv[1], "alarm")    == 0) buzz->beep(BUZZ_ALARM);
    else if (strcmp(argv[1], "charging") == 0) buzz->beep(BUZZ_CHARGING);
    else if (strcmp(argv[1], "finish")   == 0) buzz->beep(BUZZ_FINISH);
    else if (strcmp(argv[1], "error")    == 0) buzz->beep(BUZZ_ERROR);
    else if (strcmp(argv[1], "off")      == 0) buzz->stop();
    else {
        printf("Unknown: %s\r\n", argv[1]);
        return -1;
    }
    printf("buzz: %s (buzzing=%d)\r\n", argv[1], buzz->is_buzzing());
    return 0;
}

/* ================================================================
 * cmd_adc — ADC 电压读取
 *   adc        — 读通道 0 原始值 + 电压 (divider=11, vref=3300mV)
 *   adc raw    — 仅打印原始值
 * ================================================================ */
static int cmd_adc(int argc, char **argv)
{
    uint16_t raw;
    uint32_t mv;

    raw = adc->read_raw(0);
    mv  = adc->read_mv(0, 3300, 11);

    if (argc >= 2 && strcmp(argv[1], "raw") == 0) {
        printf("ADC CH0 raw: %u / 4096\r\n", raw);
    } else {
        printf("ADC CH0: raw=%u / 4096, V=%lu mV (%lu.%03lu V)\r\n",
               raw, mv, mv / 1000, mv % 1000);
    }
    return 0;
}

/* ================================================================
 * cmd_line_work — 命令行主循环 (由 cmd_task 轮询调用)
 * ================================================================ */
void cmd_line_work(void)
{
    uint8_t byte;
    int     argc, i;
    char   *argv[MAX_ARGC];
    char    copy[CMD_BUF_LEN];

    switch (sta) {
    case 0:   /* ---- 打印提示符 ---- */
        printf("\r\n>");
        memset(cmd_buf, 0, sizeof(cmd_buf));
        cmd_len = 0;
        sta = 1;
        break;

    case 1:   /* ---- 收字符 / 处理 ---- */
        if (uart0->read_rx_buf(&byte, 1) == 0)
            break;

        if (cmd_len >= CMD_BUF_LEN) {
            sta = 0;
            break;
        }

        switch (byte) {
        case 0x09:          /* TAB — 暂不处理 */
            break;

        case 0x7F:          /* Backspace / DEL */
        case '\b':
            if (cmd_len > 0) {
                cmd_len--;
                cmd_buf[cmd_len] = '\0';
            }
            printf("\r\n>%s", cmd_buf);
            break;

        case '\n':          /* LF — 忽略, 等 \r */
            if (cmd_len == 0)
                sta = 0;
            break;

        case '\r':          /* CR — 执行命令 */
            cmd_buf[cmd_len] = '\0';

            if (cmd_len == 0) {
                sta = 0;
                break;
            }

            memcpy(copy, cmd_buf, cmd_len + 1);
            argc = split_string_n(MAX_ARGC, copy, argv, " \t\n\r");

            if (argc == 0) {
                sta = 0;
                break;
            }

            /* 匹配命令表 */
            {
                int found = 0;

                for (i = 0; i < (int)ITEM_NUM(cmd_list); i++) {
                    if (strcmp(argv[0], cmd_list[i].name) == 0) {
                        printf("\r\n");
                        cmd_list[i].entry(argc, argv);
                        found = 1;
                        break;
                    }
                }

                if (!found)
                    printf("\r\n  unknown command: %s\r\n", argv[0]);
            }
            sta = 0;
            break;

        default:            /* 普通字符 — 回显 */
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
