/**
 * IO 引脚配置模块
 *
 * 统一管理板上全部 GPIO 引脚的定义、初始化和状态打印。
 * 所有引脚信息集中在 io_table[] 中，新增引脚只需加一行。
 */

#ifndef __IO_CONFIG_H__
#define __IO_CONFIG_H__

#include "SWM320.h"

/*---- 物理封装引脚号 (1~64) → drvp_gpio->write/pin/read 使用 ----*/
#define PIN_ELC0       26   /* M2  — 继电器0 */
#define PIN_ELC1        3   /* B12 — 继电器1 */
#define PIN_SLED       17   /* C4  — 状态 LED */
#define PIN_NETRST     56   /* N7  — 4G模块复位 */
#define PIN_FUSE0      21   /* C2  — 保险丝检测 */
#define PIN_HC595_DAT  36   /* P1  — HC595 数据线 */
#define PIN_HC595_CLK  35   /* P0  — HC595 时钟线 */
#define PIN_HC595_UD   37   /* P2  — HC595 锁存线 */
#define PIN_HLW0       22   /* C3  — HLW8012 插座0 脉冲输入 */
#define PIN_HLW1       20   /* C7  — HLW8012 插座1 脉冲输入 */

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

/*---- HLW8012 引脚端口映射 ----*/
#define IO_HLW0_PORT    PORTC
#define IO_HLW0_PIN     PIN3
#define IO_HLW0_FUNC    PORTC_PIN3_GPIO

#define IO_HLW1_PORT    PORTC
#define IO_HLW1_PIN     PIN7
#define IO_HLW1_FUNC    PORTC_PIN7_GPIO

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
#define IO_COUNT  7
extern const io_info_t io_table[IO_COUNT];

/*---- API ----*/
void io_init_all(void);
void io_print_all(void);

#endif
