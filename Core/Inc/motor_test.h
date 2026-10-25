#ifndef MOTOR_TEST_H
#define MOTOR_TEST_H

#include "motor.h"
#include "app_config.h"

/*
 * 作业二：TB6612 电机 PWM/方向波形测试
 *
 * 底层电机驱动已经收进 motor.c：本文件只决定“什么时候、以多大占空比、
 * 往哪个方向转”，不再直接写 GPIO 或 TIM 寄存器。
 *
 * TIM1：PSC=0，ARR=399，8 MHz 下 PWM 频率 20 kHz。
 */

/* 0 = 自动循环：正转 30/60/90%，反转 30/60/90%，每档保持 5 s
 * 1 = 固定状态：按下面的固定参数一直输出，适合慢慢拍照
 * 也可以在编译命令里加 -DMOTOR_TEST_MODE=1 覆盖此默认值。 */
#ifndef MOTOR_TEST_MODE
#define MOTOR_TEST_MODE                0
#endif

/* 自动模式的时间参数 */
#define MOTOR_TEST_STEP_MS             5000U   /* 每档占空比保持 5 s */
#define MOTOR_TEST_STEP_GAP_MS         500U    /* 档间停止 0.5 s，便于示波器分档观察 */
#define MOTOR_TEST_START_DELAY_MS      1000U   /* 上电延时 1 s */

/* 自动模式的三档占空比，作业要求 30 / 60 / 90 % */
#define MOTOR_TEST_DUTY_1_PERCENT      30U
#define MOTOR_TEST_DUTY_2_PERCENT      60U
#define MOTOR_TEST_DUTY_3_PERCENT      90U

/* 固定模式参数（仅 MOTOR_TEST_MODE == 1 时生效）
 * 方向取值：0=停止，1=正转，2=反转，3=刹车 */
#define MOTOR_TEST_FIXED_LEFT_DIR      MOTOR_DIR_FORWARD
#define MOTOR_TEST_FIXED_RIGHT_DIR     MOTOR_DIR_FORWARD
#define MOTOR_TEST_FIXED_LEFT_DUTY     60U
#define MOTOR_TEST_FIXED_RIGHT_DUTY    60U

/* 停止两路电机、清零占空比并启动 PWM 和编码器。
 * 必须在 CubeMX 生成的 MX_GPIO_Init()、MX_TIM1_Init() 之后调用。 */
void MotorTest_Init(void);

/* 周期调用：自动模式负责换向/换档，固定模式只在上电后应用一次参数 */
void MotorTest_Task(void);

#endif
