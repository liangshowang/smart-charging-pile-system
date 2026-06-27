/**
 * 帧解析模块
 *
 * 帧格式 (12 字节):
 *   [0]  0xA5  帧头
 *   [1]  CMD   命令码
 *   [2..9]     8 字节数据
 *   [10]       校验和 (字节 1~9 累加取低 8 位)
 *   [11] 0x5A  帧尾
 *
 * 状态机:
 *   WAIT_HEAD → 收到 0xA5 → WAIT_BODY → 收满 12 字节 →
 *   验帧尾 → 验校验和 → 派发 → WAIT_HEAD
 */

#ifndef __FRAME_PARSER_H__
#define __FRAME_PARSER_H__

#include <stdint.h>

#define FRAME_LEN   12
#define FRAME_HEAD  0xA5
#define FRAME_TAIL  0x5A

/* 命令码定义 */
#define CMD_LED_ON   0x01    /* 开状态灯 */
#define CMD_LED_OFF  0x02    /* 关状态灯 */
#define CMD_RELAY0   0x03    /* 插座1 继电器 (data[0]=1吸合, 0断开) */
#define CMD_RELAY1   0x04    /* 插座2 继电器 (data[0]=1吸合, 0断开) */
#define CMD_PING     0xAA    /* 心跳/测试 */

void frame_parser_poll(void);

#endif
