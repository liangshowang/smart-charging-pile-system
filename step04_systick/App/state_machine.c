#include "SWM320.h"
#include "systick.h"
#include "io_config.h"
#include "state_machine.h"
#include <stdio.h>

/*---- 定时参数 (单位: ms) ----*/
#define T_OPEN_MS   5000    /* 继电器吸合保持时间 */
#define T_GAP_MS    2000    /* 继电器切换间隔     */

/*---- 状态号 ----*/
enum {
    SM_INIT = 0,
    SM_ELC0_ON,
    SM_ELC0_WAIT,
    SM_ELC0_OFF,
    SM_ELC0_GAP,
    SM_ELC1_ON,
    SM_ELC1_WAIT,
    SM_ELC1_OFF,
    SM_ELC1_GAP,
};

static int      sm_state = SM_INIT;
static uint32_t sm_tick  = 0;   /* 进入当前状态时的 tick */

void sm_init(void)
{
    sm_state = SM_INIT;
    sm_tick  = 0;
}

void sm_work(void)
{
    uint32_t now = get_systick();

    switch (sm_state)
    {
    case SM_INIT:
        printf("[SM] 状态机启动，全部断开\r\n");
        GPIO_ClrBit(GPIOM, PIN2);    /* ELC0 OFF */
        GPIO_ClrBit(GPIOB, PIN12);   /* ELC1 OFF */
        sm_tick  = now;
        sm_state = SM_ELC0_ON;
        break;

    case SM_ELC0_ON:
        printf("[SM] ELC0 吸合 (t=%d ms)\r\n", now);
        GPIO_SetBit(GPIOM, PIN2);
        sm_tick  = now;
        sm_state = SM_ELC0_WAIT;
        break;

    case SM_ELC0_WAIT:
        if ((now - sm_tick) >= T_OPEN_MS)
            sm_state = SM_ELC0_OFF;
        break;

    case SM_ELC0_OFF:
        printf("[SM] ELC0 断开 (t=%d ms)\r\n", now);
        GPIO_ClrBit(GPIOM, PIN2);
        sm_tick  = now;
        sm_state = SM_ELC0_GAP;
        break;

    case SM_ELC0_GAP:
        if ((now - sm_tick) >= T_GAP_MS)
            sm_state = SM_ELC1_ON;
        break;

    case SM_ELC1_ON:
        printf("[SM] ELC1 吸合 (t=%d ms)\r\n", now);
        GPIO_SetBit(GPIOB, PIN12);
        sm_tick  = now;
        sm_state = SM_ELC1_WAIT;
        break;

    case SM_ELC1_WAIT:
        if ((now - sm_tick) >= T_OPEN_MS)
            sm_state = SM_ELC1_OFF;
        break;

    case SM_ELC1_OFF:
        printf("[SM] ELC1 断开 (t=%d ms)\r\n", now);
        GPIO_ClrBit(GPIOB, PIN12);
        sm_tick  = now;
        sm_state = SM_ELC1_GAP;
        break;

    case SM_ELC1_GAP:
        if ((now - sm_tick) >= T_GAP_MS)
        {
            printf("===== 一轮完成 =====\r\n\r\n");
            sm_state = SM_ELC0_ON;
        }
        break;

    default:
        sm_state = SM_INIT;
        break;
    }
}
