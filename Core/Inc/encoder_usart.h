#ifndef ENCODER_USART_H
#define ENCODER_USART_H

#include "motor.h"

/*
 * 编码器串口输出
 *
 * 数据来源：Motor_GetCount()，所以主循环必须先调用 Motor_Update()。
 * 输出：USART1（PA9=TX，115200 8N1）为主，同时镜像到 USART3（PB10=TX）。
 *
 * 默认只在左右轮计数发生变化时发送，车轮停稳后不再刷屏。
 * 需要每 100 ms 无条件输出时，把 ENCODER_USART_ALWAYS_PRINT 改为 1。
 */

#define ENCODER_USART_PERIOD_MS        100U
#define ENCODER_USART_ALWAYS_PRINT     0

/* 初始化一个电机的串口输出；左右各调用一次，必须在 Motor_Init() 之后。 */
void EncoderUsart_Init(Motor *motor);

/* 主循环周期调用；内部按 ENCODER_USART_PERIOD_MS 限速。 */
void EncoderUsart_Task(void);

#endif
