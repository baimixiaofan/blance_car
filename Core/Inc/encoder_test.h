#ifndef ENCODER_TEST_H
#define ENCODER_TEST_H

#include "motor.h"
#include "app_config.h"

/*
 * 作业三：AB 编码器计圈/测速测试
 *
 * 编码器归属和 1560 计数/车轮圈的换算都在 motor.h 里：
 *   左轮：PB6(A)/PB7(B) -> TIM4
 *   右轮：PA0(B)/PA1(A) -> TIM2
 * 本文件只负责采样、串口输出和测试流程。
 */

/* 采样和打印周期 */
#define ENCODER_SAMPLE_MS              100U

/* 空载测试参数：电机持续转动，直到复位/断电 */
#define ENCODER_MOTOR_START_DELAY_MS   1000U
#define ENCODER_MOTOR_DUTY_PERCENT     100U
#define ENCODER_MOTOR_RATED_RPM        366L

/* 调试模式参数：左右电机持续转动，直到复位或断电 */
#define ENCODER_DEBUG_DUTY_PERCENT     50U

/* 四圈往返任务参数 */
#define ENCODER_FOUR_TURN_TURNS        4U
#define ENCODER_FOUR_TURN_DUTY_PERCENT 50U
#define ENCODER_FOUR_TURN_GAP_MS       500U
#define ENCODER_FOUR_TURN_TIMEOUT_MS   5000U

void EncoderTest_Init(void);
void EncoderTest_Task(void);

#endif
