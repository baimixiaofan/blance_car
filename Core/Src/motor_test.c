/*
 * TB6612 双电机测试模块
 *
 * 引脚、GPIO 和 TIM1 的初始化全部由 CubeMX 生成的 MX_GPIO_Init()、
 * MX_TIM1_Init() 完成；本文件只通过 HAL 启停 PWM、修改 CCR 和写方向线，
 * 不再直接操作 GPIO/TIM 寄存器。
 *
 * CubeMX 配置：
 *   右电机：PA8/TIM1_CH1 -> PWMA，PB14/PB15 -> AIN1/AIN2
 *   左电机：PA11/TIM1_CH4 -> PWMB，PB12/PB13 -> BIN1/BIN2
 *   TIM1：PSC=0，ARR=399，PWM Mode 1，8 MHz 下为 20 kHz
 *
 * 当前默认通过 MOTOR_TEST_USE_RIGHT_MOTOR=0 只驱动左电机；
 * 恢复双电机时只需改宏，不需要改 CubeMX 配置。
 */

#include "main.h"
#include "motor_test.h"

/* CubeMX 在 main.c 中定义的 TIM1 句柄 */
extern TIM_HandleTypeDef htim1;

/*
 * 按指定方向写一个电机的 IN1/IN2。
 * dir=停止/刹车时两线同时动作，正转/反转时一高一低。
 * 方向改变只在占空比为 0 时进行（由测试状态机保证），因此两次写引脚
 * 之间的几个时钟周期不会造成带载换向。
 */
static void set_motor_dir(MotorDir dir, GPIO_TypeDef *port, uint16_t in1_pin, uint16_t in2_pin,
                          uint8_t inverted)
{
    if (inverted != 0U)
    {
        if (dir == MOTOR_DIR_FORWARD)
        {
            dir = MOTOR_DIR_REVERSE;
        }
        else if (dir == MOTOR_DIR_REVERSE)
        {
            dir = MOTOR_DIR_FORWARD;
        }
    }

    switch (dir)
    {
        case MOTOR_DIR_FORWARD:     /* IN1=1, IN2=0 */
            HAL_GPIO_WritePin(port, in1_pin, GPIO_PIN_SET);
            HAL_GPIO_WritePin(port, in2_pin, GPIO_PIN_RESET);
            break;
        case MOTOR_DIR_REVERSE:     /* IN1=0, IN2=1 */
            HAL_GPIO_WritePin(port, in1_pin, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(port, in2_pin, GPIO_PIN_SET);
            break;
        case MOTOR_DIR_BRAKE:       /* IN1=IN2=1，短接制动 */
            HAL_GPIO_WritePin(port, in1_pin, GPIO_PIN_SET);
            HAL_GPIO_WritePin(port, in2_pin, GPIO_PIN_SET);
            break;
        case MOTOR_DIR_STOP:        /* IN1=IN2=0，输出关闭 */
        default:
            HAL_GPIO_WritePin(port, in1_pin, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(port, in2_pin, GPIO_PIN_RESET);
            break;
    }
}

void MotorTest_Init(void)
{
    /*
     * CubeMX 已经配置好 GPIO 和 TIM1，这里只做运行前的安全状态：
     * 方向先回停止、两路占空比清零，再启动对应 PWM 通道。
     * HAL_TIM_PWM_Start 会打开 CCxE 和 TIM1 的 MOE。
     */
    MotorTest_SetDirection(MOTOR_DIR_STOP, MOTOR_DIR_STOP);
    MotorTest_SetDutyPercent(0U, 0U);

#if (MOTOR_TEST_USE_LEFT_MOTOR != 0)
    if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_4) != HAL_OK)
    {
        Error_Handler();
    }
#endif

#if (MOTOR_TEST_USE_RIGHT_MOTOR != 0)
    if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1) != HAL_OK)
    {
        Error_Handler();
    }
#endif
}

void MotorTest_SetDirection(MotorDir left, MotorDir right)
{
#if (MOTOR_TEST_USE_LEFT_MOTOR != 0)
    /* 左电机使用 CubeMX 生成的 BIN1/BIN2 引脚标签 */
    set_motor_dir(left, BIN1_GPIO_Port, BIN1_Pin, BIN2_Pin, MOTOR_TEST_LEFT_DIR_INVERTED);
#else
    /* 左电机停用：方向线固定拉低 */
    (void)left;
    HAL_GPIO_WritePin(BIN1_GPIO_Port, BIN1_Pin | BIN2_Pin, GPIO_PIN_RESET);
#endif

#if (MOTOR_TEST_USE_RIGHT_MOTOR != 0)
    /* 右电机使用 CubeMX 生成的 AIN1/AIN2 引脚标签 */
    set_motor_dir(right, AIN1_GPIO_Port, AIN1_Pin, AIN2_Pin, MOTOR_TEST_RIGHT_DIR_INVERTED);
#else
    /* 右电机停用：方向线固定拉低 */
    (void)right;
    HAL_GPIO_WritePin(AIN1_GPIO_Port, AIN1_Pin | AIN2_Pin, GPIO_PIN_RESET);
#endif
}

void MotorTest_SetDutyPercent(uint8_t left_percent, uint8_t right_percent)
{
    uint32_t period_counts = __HAL_TIM_GET_AUTORELOAD(&htim1) + 1U;

    if (left_percent > 100U)
    {
        left_percent = 100U;
    }
    if (right_percent > 100U)
    {
        right_percent = 100U;
    }

    /*
     * 左电机接 PA11/TIM1_CH4，右电机接 PA8/TIM1_CH1，注意不要写反。
     * CCR = 周期计数 * 百分比 / 100；CCR 等于周期计数时输出恒高，即 100%。
     * HAL_TIM_PWM_ConfigChannel 已使能 CCR 预装载，新值会在下一个更新事件生效。
     * 被停用的电机强制写 0，防止它误转。
     */
#if (MOTOR_TEST_USE_LEFT_MOTOR != 0)
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_4, (period_counts * left_percent) / 100U);
#else
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_4, 0U);
#endif

#if (MOTOR_TEST_USE_RIGHT_MOTOR != 0)
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, (period_counts * right_percent) / 100U);
#else
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0U);
#endif
}

#if (MOTOR_TEST_MODE == 0)

/*
 * 自动模式状态机：
 *   s_phase=0：停止间隙，方向线保持上一档状态，等待 GAP 时间；
 *   s_phase=1：PWM 正在输出，等待 STEP 时间后把两路占空比清零。
 * 换向发生在占空比为 0 的间隙里，避免电机带载瞬间反接产生大电流。
 * 被停用的那一路在 MotorTest_SetDutyPercent/SetDirection 内部始终为 0/停止。
 */
typedef struct
{
    MotorDir direction;
    uint8_t duty_percent;
} MotorTestStep;

static const MotorTestStep s_steps[] =
{
    {MOTOR_DIR_FORWARD, MOTOR_TEST_DUTY_1_PERCENT},   /* 正转 30% */
    {MOTOR_DIR_FORWARD, MOTOR_TEST_DUTY_2_PERCENT},   /* 正转 60% */
    {MOTOR_DIR_FORWARD, MOTOR_TEST_DUTY_3_PERCENT},   /* 正转 90% */
    {MOTOR_DIR_REVERSE, MOTOR_TEST_DUTY_1_PERCENT},   /* 反转 30% */
    {MOTOR_DIR_REVERSE, MOTOR_TEST_DUTY_2_PERCENT},   /* 反转 60% */
    {MOTOR_DIR_REVERSE, MOTOR_TEST_DUTY_3_PERCENT},   /* 反转 90% */
};

static uint8_t s_started;        /* 是否已经过了上电延时 */
static uint8_t s_phase;          /* 0=间隙，1=输出中 */
static uint8_t s_step_index;     /* 当前执行到 s_steps 的哪一档 */
static uint32_t s_phase_start_ms;

void MotorTest_Task(void)
{
    uint32_t now = HAL_GetTick();

    /* 上电延时：避免上电瞬间电源不稳时电机就启动 */
    if (s_started == 0U)
    {
        if (now < MOTOR_TEST_START_DELAY_MS)
        {
            return;
        }
        s_started = 1U;
        s_phase_start_ms = now;
        return;
    }

    if (s_phase == 0U)
    {
        /* 间隙结束：先设置方向，再打开占空比 */
        if ((now - s_phase_start_ms) >= MOTOR_TEST_STEP_GAP_MS)
        {
            const MotorTestStep *step = &s_steps[s_step_index];
            MotorTest_SetDirection(step->direction, step->direction);
            MotorTest_SetDutyPercent(step->duty_percent, step->duty_percent);
            s_phase = 1U;
            s_phase_start_ms = now;
        }
    }
    else
    {
        /* 当前档保持结束：占空比清零后进入间隙 */
        if ((now - s_phase_start_ms) >= MOTOR_TEST_STEP_MS)
        {
            MotorTest_SetDutyPercent(0U, 0U);
            s_step_index++;
            if (s_step_index >= (uint8_t)(sizeof(s_steps) / sizeof(s_steps[0])))
            {
                s_step_index = 0U;
            }
            s_phase = 0U;
            s_phase_start_ms = now;
        }
    }
}

#else

/*
 * 固定模式：上电延时后只应用一次参数，之后保持输出不变，
 * 适合把某一档波形固定在屏幕上慢慢拍照。
 */
static uint8_t s_fixed_applied;

void MotorTest_Task(void)
{
    if (s_fixed_applied == 0U)
    {
        if (HAL_GetTick() < MOTOR_TEST_START_DELAY_MS)
        {
            return;
        }
        MotorTest_SetDirection((MotorDir)MOTOR_TEST_FIXED_LEFT_DIR,
                               (MotorDir)MOTOR_TEST_FIXED_RIGHT_DIR);
        MotorTest_SetDutyPercent(MOTOR_TEST_FIXED_LEFT_DUTY, MOTOR_TEST_FIXED_RIGHT_DUTY);
        s_fixed_applied = 1U;
    }
}

#endif
