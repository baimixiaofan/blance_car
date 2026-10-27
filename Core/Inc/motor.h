#ifndef MOTOR_H
#define MOTOR_H

#include "main.h"
#include <stdint.h>

/*
 * 电机 + 编码器对象
 *
 * 硬件映射（CubeMX 已配置好，本模块只调用 HAL）：
 *   左电机：PWM = PA8/TIM1_CH1， 方向 = PB14/PB15，编码器 = PB6/PB7/TIM4
 *   右电机：PWM = PA11/TIM1_CH4，方向 = PB12/PB13，编码器 = PA15/PB3/TIM2（全重映射）
 *
 * 编码器：13 线 × 30:1 减速比 × 4 倍频 = 1560 计数/车轮圈。
 * “圈数”统一指车轮/输出轴圈数，不是电机轴圈数。
 */

#define MOTOR_COUNTS_PER_REV        1560L
#define MOTOR_DEFAULT_TIMEOUT_MS    5000U

/* 如果实测方向与标注相反，把对应宏改为 1，不需要改接线。 */
#define MOTOR_LEFT_DIR_INVERTED     0
/* 右通道方向线物理反接（IN1/IN2 与左通道对调），置 1 后左右都用 MOTOR_DIR_FORWARD */
#define MOTOR_RIGHT_DIR_INVERTED    1
#define MOTOR_LEFT_ENC_INVERTED     0
#define MOTOR_RIGHT_ENC_INVERTED    0

typedef enum
{
    MOTOR_LEFT = 0,
    MOTOR_RIGHT = 1,
    MOTOR_ID_COUNT
} MotorId;

typedef enum
{
    MOTOR_DIR_STOP = 0,      /* 停止：IN1=IN2=0，输出关闭 */
    MOTOR_DIR_FORWARD = 1,   /* 正转：IN1=1, IN2=0 */
    MOTOR_DIR_REVERSE = 2,   /* 反转：IN1=0, IN2=1 */
    MOTOR_DIR_BRAKE = 3      /* 刹车：IN1=IN2=1 */
} MotorDir;

typedef enum
{
    MOTOR_OK = 0,        /* 空闲，或转动已完成 */
    MOTOR_BUSY,          /* 正在执行 MoveTurns/RotateTurns */
    MOTOR_TIMEOUT,       /* 超时，已自动停止 */
    MOTOR_ERR_PARAM,     /* 参数错误，或模块未初始化 */
    MOTOR_ERR_BUSY       /* 上一次转动还没结束 */
} MotorStatus;

typedef struct Motor Motor;

/*
 * 初始化指定电机，返回它的指针；id 非法时返回 NULL。
 * 重复调用同一个 id 会直接返回同一个指针，不会重复初始化。
 * 初始化内容：方向停止、占空比 0、启动对应 PWM 通道、启动并清零编码器。
 */
Motor *Motor_Init(MotorId id);

/* 设置方向。运行中换向前应先 Motor_SetSpeed(motor, 0)。 */
void Motor_SetDirection(Motor *motor, MotorDir dir);

/* 设置占空比，范围 0~100，超过 100 按 100 处理。 */
void Motor_SetSpeed(Motor *motor, uint8_t percent);

/* 先清零占空比、再换向、最后设置新占空比，避免带载换向。 */
void Motor_SetDirSpeed(Motor *motor, MotorDir dir, uint8_t percent);

/* 占空比清零并关闭输出（滑行停止），同时取消尚未完成的转动。 */
void Motor_Stop(Motor *motor);

/* 短接制动，保持当前 PWM 占空比不变。 */
void Motor_Brake(Motor *motor);

/*
 * 非阻塞转动：立即返回 MOTOR_BUSY。
 * 之后必须在主循环里持续调用 Motor_Update()，它会在到达目标圈数或超时后
 * 自动停车。timeout_ms 传 0 表示不超时（不建议）。
 * 到达目标计数后立即关输出，机械惯性可能造成少量过冲；要更准可降低速度。
 */
MotorStatus Motor_MoveTurns(Motor *motor, uint8_t speed_percent, MotorDir dir,
                            float turns, uint32_t timeout_ms);

/*
 * 阻塞转动：函数内部循环调用 Motor_Update()，直到转完或超时才返回
 * MOTOR_OK / MOTOR_TIMEOUT。适合只让一个电机转的场合。
 * 左右轮要同时转时请用 Motor_MoveTurns() + Motor_Update()。
 */
MotorStatus Motor_RotateTurns(Motor *motor, uint8_t speed_percent, MotorDir dir,
                              float turns, uint32_t timeout_ms);

/* 使用默认 5 s 超时的简化版本。 */
MotorStatus Motor_Move(Motor *motor, uint8_t speed_percent, MotorDir dir, float turns);
MotorStatus Motor_Rotate(Motor *motor, uint8_t speed_percent, MotorDir dir, float turns);

/*
 * 读取编码器、累计计数并推进转动状态。主循环里每 1~10 ms 调用一次。
 * 16 位计数器会回绕，这里按增量累计到 int32，正常调用频率不会丢计数。
 * 返回 MOTOR_BUSY 表示还在转，MOTOR_OK/MOTOR_TIMEOUT 表示已结束。
 */
MotorStatus Motor_Update(Motor *motor);

uint8_t Motor_IsBusy(const Motor *motor);
MotorStatus Motor_GetStatus(const Motor *motor);

/* 返回电机编号；motor == NULL 时返回 MOTOR_ID_COUNT。 */
MotorId Motor_GetId(const Motor *motor);

/* 累计计数（带符号，正转增加），由 Motor_Update() 更新。 */
int32_t Motor_GetCount(const Motor *motor);
uint16_t Motor_GetRawCounter(const Motor *motor);
float Motor_GetTurns(const Motor *motor);

/* 停止电机、清零累计计数和硬件计数器。 */
void Motor_ResetCount(Motor *motor);

#endif
