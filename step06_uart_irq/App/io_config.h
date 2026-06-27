/**
 * IO 引脚配置模块
 *
 * 统一管理板上全部 GPIO 引脚的定义、初始化和状态打印。
 * 所有引脚信息集中在 io_table[] 中，新增引脚只需加一行。
 */

#ifndef __IO_CONFIG_H__
#define __IO_CONFIG_H__

#include "SWM320.h"

/*---- 引脚封装编号 → 端口映射 ----*/
#define IO_ELC0_PORT    PORTM
#define IO_ELC0_PIN     PIN2
#define IO_ELC0_FUNC    PORTM_PIN2_GPIO

#define IO_ELC1_PORT    PORTB
#define IO_ELC1_PIN     PIN12
#define IO_ELC1_FUNC    PORTB_PIN12_GPIO

#define IO_SLED_PORT    PORTC
#define IO_SLED_PIN     PIN4
#define IO_SLED_FUNC    PORTC_PIN4_GPIO

#define IO_NETRST_PORT  PORTN
#define IO_NETRST_PIN   PIN7
#define IO_NETRST_FUNC  PORTN_PIN7_GPIO

#define IO_FUSE_PORT    PORTC
#define IO_FUSE_PIN     PIN2
#define IO_FUSE_FUNC    PORTC_PIN2_GPIO

/*---- IO 信息结构体 ----*/
#define PIN_DIR_OUT  0
#define PIN_DIR_IN   1

typedef struct {
    const char      *name;
    uint32_t         port;
    uint32_t         pin;
    uint32_t         func;
    GPIO_TypeDef    *gpio;
    uint32_t         dir;
} io_info_t;

/*---- 全局 IO 表 ----*/
#define IO_COUNT  5
extern const io_info_t io_table[IO_COUNT];

/*---- API ----*/
void io_init_all(void);
void io_print_all(void);

#endif
