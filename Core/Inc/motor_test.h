#ifndef MOTOR_TEST_H
#define MOTOR_TEST_H

#include "stm32f1xx_hal.h"

/*
 * 硬件连接与 CubeMX 配置（与《引脚配置.md》一致）：
 *   右电机：PA8  -> TIM1_CH1 -> PWMA，标签 PWMA
 *           PB14 -> AIN1，PB15 -> AIN2，标签 AIN1/AIN2
 *   左电机：PA11 -> TIM1_CH4 -> PWMB，标签 PWMB
 *           PB12 -> BIN1，PB13 -> BIN2，标签 BIN1/BIN2
 *
 * TIM1（CubeMX 中配置）：PSC=0，ARR=399，PWM Mode 1，CH1/CH4 使能，
 * 8 MHz 时钟下 PWM 频率 = 20 kHz。要改频率请在 CubeMX 的 TIM1 参数里改。
 *
 * 方向真值表（PWM 为高时）：
 *   IN1=1, IN2=0 -> 正转，OUT1 输出 PWM，OUT2 为低
 *   IN1=0, IN2=1 -> 反转，OUT2 输出 PWM，OUT1 为低
 *   IN1=0, IN2=0 -> 停止（输出关闭）
 *   IN1=1, IN2=1 -> 刹车（两端同时为低）
 */
typedef enum
{
    MOTOR_DIR_STOP = 0,      /* 停止：方向线全低，输出关闭 */
    MOTOR_DIR_FORWARD = 1,   /* 正转：IN1=1, IN2=0 */
    MOTOR_DIR_REVERSE = 2,   /* 反转：IN1=0, IN2=1 */
    MOTOR_DIR_BRAKE = 3      /* 刹车：IN1=IN2=1 */
} MotorDir;

/* ======================= 测试参数（改这里即可） ======================= */

/* 0 = 自动循环：正转 30/60/90%，反转 30/60/90%，每档保持 5 s
 * 1 = 固定状态：按下面的固定参数一直输出，适合慢慢拍照
 * 也可以在编译命令里加 -DMOTOR_TEST_MODE=1 覆盖此默认值。 */
#ifndef MOTOR_TEST_MODE
#define MOTOR_TEST_MODE                0
#endif

/* 自动模式的时间参数 */
#define MOTOR_TEST_STEP_MS             5000U   /* 每档占空比保持 5 s */
#define MOTOR_TEST_STEP_GAP_MS         500U    /* 档间停止 0.5 s，便于示波器分档观察 */
#define MOTOR_TEST_START_DELAY_MS      1000U   /* 上电延时 1 s，给电源和调试器留时间 */

/* 自动模式的三档占空比，作业要求 30 / 60 / 90 % */
#define MOTOR_TEST_DUTY_1_PERCENT      30U
#define MOTOR_TEST_DUTY_2_PERCENT      60U
#define MOTOR_TEST_DUTY_3_PERCENT      90U

/* 单路测试开关：
 * 某一路不接电机或暂时不测时，把对应宏改成 0，程序会一直把它保持在停止状态；
 * 该路的方向线会保持低电平，PWM 占空比固定为 0。
 * 当前默认只驱动左电机（右电机已停用），恢复双电机测试时把右电机改为 1。 */
#ifndef MOTOR_TEST_USE_LEFT_MOTOR
#define MOTOR_TEST_USE_LEFT_MOTOR      1
#endif
#ifndef MOTOR_TEST_USE_RIGHT_MOTOR
#define MOTOR_TEST_USE_RIGHT_MOTOR     0
#endif

/* 固定模式参数（仅 MOTOR_TEST_MODE == 1 时生效）
 * 例：只拍右电机反转 60%、左电机停：
 *   MOTOR_TEST_FIXED_LEFT_DIR   = MOTOR_DIR_STOP
 *   MOTOR_TEST_FIXED_RIGHT_DIR  = MOTOR_DIR_REVERSE
 *   MOTOR_TEST_FIXED_LEFT_DUTY  = 0
 *   MOTOR_TEST_FIXED_RIGHT_DUTY = 60
 * 方向取值：0=停止，1=正转，2=反转，3=刹车 */
#define MOTOR_TEST_FIXED_LEFT_DIR      MOTOR_DIR_FORWARD
#define MOTOR_TEST_FIXED_RIGHT_DIR     MOTOR_DIR_FORWARD
#define MOTOR_TEST_FIXED_LEFT_DUTY     60U
#define MOTOR_TEST_FIXED_RIGHT_DUTY    60U

/* 若实测电机转向与“正转”标注相反，把对应宏改为 1。
 * 它只交换 IN1/IN2 的高低电平，不影响 PWM 频率和占空比。 */
#define MOTOR_TEST_LEFT_DIR_INVERTED   0
#define MOTOR_TEST_RIGHT_DIR_INVERTED  0

/* ============================== 对外接口 ============================== */

/* 停止两路电机并启动 TIM1 已使能的 PWM 通道，初始占空比为 0。
 * 必须在 CubeMX 生成的 MX_GPIO_Init() 和 MX_TIM1_Init() 之后调用。 */
void MotorTest_Init(void);

/* 周期调用：自动模式负责换向/换档，固定模式只在上电后应用一次参数 */
void MotorTest_Task(void);

/* 立即设置左右方向（运行中调用时应先保证占空比为 0） */
void MotorTest_SetDirection(MotorDir left, MotorDir right);

/* 立即设置左右占空比，范围 0~100，超过 100 按 100 处理 */
void MotorTest_SetDutyPercent(uint8_t left_percent, uint8_t right_percent);

#endif
