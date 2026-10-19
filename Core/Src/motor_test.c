/*
 * TB6612 双电机测试模块
 *
 * 右电机（通道 A）：PA8/TIM1_CH1 -> PWMA，PB14/PB15 -> AIN1/AIN2
 * 左电机（通道 B）：PA11/TIM1_CH4 -> PWMB，PB12/PB13 -> BIN1/BIN2
 *
 * TIM1 和 GPIO 在这里直接配置，不依赖 CubeMX 生成的初始化代码，
 * 因此重新生成工程不会破坏测试功能。
 *
 * 当前默认通过 MOTOR_TEST_USE_RIGHT_MOTOR=0 只驱动左电机；
 * 右电机的引脚定义仍然保留，恢复双电机时只需改宏，不要注释下面的引脚宏。
 */

#include "motor_test.h"

/* 方向线引脚分配（与电机测试接线表一致） */
#define LEFT_IN1_PIN    GPIO_PIN_12   /* PB12 */
#define LEFT_IN2_PIN    GPIO_PIN_13   /* PB13 */
#define RIGHT_IN1_PIN   GPIO_PIN_14   /* PB14 */
#define RIGHT_IN2_PIN   GPIO_PIN_15   /* PB15 */

/* PWM 一个周期的计数个数，即 ARR + 1，设置占空比时用它换算 CCR */
static uint32_t s_pwm_period_counts;

/*
 * 计算 TIM1 的计数时钟。
 * STM32F1 中 APB2 预分频不为 1 时，定时器时钟 = PCLK2 * 2；为 1 时就是 PCLK2。
 * 这样即使以后把系统时钟改成 72 MHz，PWM 频率仍会按 20 kHz 重新计算。
 */
static uint32_t motor_timer_clock_hz(void)
{
    uint32_t pclk2 = HAL_RCC_GetPCLK2Freq();
    return ((RCC->CFGR & RCC_CFGR_PPRE2) == RCC_CFGR_PPRE2_DIV1) ? pclk2 : (pclk2 * 2U);
}

/*
 * 把“方向 + 是否反接”转换成 GPIO BSRR 的置位/复位掩码。
 * set 中为 1 的位输出高电平，reset 中为 1 的位输出低电平。
 */
static void motor_dir_bits(MotorDir dir, uint16_t in1, uint16_t in2, uint8_t inverted,
                           uint32_t *set, uint32_t *reset)
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
            *set = in1;
            *reset = in2;
            break;
        case MOTOR_DIR_REVERSE:     /* IN1=0, IN2=1 */
            *set = in2;
            *reset = in1;
            break;
        case MOTOR_DIR_BRAKE:       /* IN1=IN2=1，短接制动 */
            *set = (uint32_t)in1 | in2;
            *reset = 0U;
            break;
        case MOTOR_DIR_STOP:        /* IN1=IN2=0，输出关闭 */
        default:
            *set = 0U;
            *reset = (uint32_t)in1 | in2;
            break;
    }
}

void MotorTest_Init(void)
{
    /* 1. 打开 GPIOA、GPIOB 和 TIM1 的时钟 */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_TIM1_CLK_ENABLE();

    /*
     * 2. 方向线先输出低电平，再切换为推挽输出，保证上电瞬间两个电机都不转。
     *    即使右电机被停用，也把 PB14/PB15 拉低并配置成输出，避免引脚浮空。
     *    BSRR 的高 16 位写 1 会复位对应引脚；这里用一次写操作，避免读改写毛刺。
     */
    GPIOB->BSRR = ((uint32_t)(LEFT_IN1_PIN | LEFT_IN2_PIN | RIGHT_IN1_PIN | RIGHT_IN2_PIN)) << 16U;

    /*
     * GPIOx_CRH 每 4 位配置一个引脚（引脚号 8~15）：
     *   0x3 = 通用推挽输出、50 MHz。
     * PB12~PB15 对应 CRH 的 bit16~bit31，所以整体写入 0x3333 << 16。
     * 先读出来清掉这 16 位，再写配置，避免影响 PB8~PB11。
     */
    GPIOB->CRH = (GPIOB->CRH & ~(0xFFFFUL << 16U)) | (0x3333UL << 16U);

    /*
     * 3. PA8 -> TIM1_CH1（右电机），PA11 -> TIM1_CH4（左电机）。
     *    PA8 对应 CRH 的 bit0~bit3，PA11 对应 bit12~bit15。
     *    0xB = 复用推挽输出、50 MHz；只改这两位，保留 PA9/PA10/PA12~PA15 的配置。
     *    右电机停用时 CCR1 始终保持 0，所以 PA8 只会输出低电平。
     */
    GPIOA->CRH = (GPIOA->CRH & ~((0xFUL << 0U) | (0xFUL << 12U)))
               | (0xBUL << 0U) | (0xBUL << 12U);

    /*
     * 4. 计算 ARR，得到 20 kHz：
     *      一个周期的计数个数 = 计数时钟 / PWM 频率
     *      ARR = 计数个数 - 1
     *    加半个频率再除，是为了四舍五入。
     *    8 MHz 时 ARR=399（400 个计数），64 MHz 时 ARR=3199（3200 个计数）。
     */
    uint32_t tim_clk = motor_timer_clock_hz();
    uint32_t arr = (tim_clk + MOTOR_TEST_PWM_FREQUENCY_HZ / 2U) / MOTOR_TEST_PWM_FREQUENCY_HZ - 1U;
    s_pwm_period_counts = arr + 1U;

    /*
     * 5. 初始化 TIM1：
     *    - PSC=0，定时器不分频
     *    - PWM 模式 1：CNT < CCR 时输出高电平
     *    - 打开 CCR 预装载（OCxPE）：运行中修改占空比会在下一个更新事件生效，
     *      不会切断当前正在输出的脉冲
     *    - CCER 打开 CH1 和 CH4 输出
     *    - BDTR 的 MOE 是高级定时器 TIM1 的主输出使能，必须置 1
     *    - EGR 的 UG 产生一次更新，把 PSC/ARR/CCR 装入影子寄存器
     */
    TIM1->CR1 = 0U;             /* 先停止计数器，边沿对齐、向上计数 */
    TIM1->PSC = 0U;             /* 计数器时钟 = TIM1 时钟 */
    TIM1->ARR = arr;            /* PWM 周期 */
    TIM1->CCR1 = 0U;            /* 右电机初始占空比 0% */
    TIM1->CCR4 = 0U;            /* 左电机初始占空比 0% */

    TIM1->CCMR1 = TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1 | TIM_CCMR1_OC1PE;
    TIM1->CCMR2 = TIM_CCMR2_OC4M_2 | TIM_CCMR2_OC4M_1 | TIM_CCMR2_OC4PE;
    TIM1->CCER = TIM_CCER_CC1E | TIM_CCER_CC4E;   /* 高电平有效，使能 CH1/CH4 */
    TIM1->BDTR = TIM_BDTR_MOE;                    /* 主输出使能 */

    TIM1->EGR = TIM_EGR_UG;     /* 手动产生更新事件，立即装载上面的配置 */
    TIM1->SR = 0U;              /* 清除 UG 产生的更新标志 */

    /* 6. 启动计数器；此时两路 CCR 均为 0，电机不会动 */
    TIM1->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
}

void MotorTest_SetDirection(MotorDir left, MotorDir right)
{
    uint32_t set = 0U;
    uint32_t reset = 0U;
    uint32_t left_set = 0U;
    uint32_t left_reset = 0U;
    uint32_t right_set = 0U;
    uint32_t right_reset = 0U;

#if (MOTOR_TEST_USE_LEFT_MOTOR != 0)
    /* 左电机使能：按请求的方向计算 BIN1/BIN2 */
    motor_dir_bits(left, LEFT_IN1_PIN, LEFT_IN2_PIN, MOTOR_TEST_LEFT_DIR_INVERTED,
                   &left_set, &left_reset);
#else
    /* 左电机停用：方向线固定拉低，输出关闭 */
    (void)left;
    left_reset = (uint32_t)LEFT_IN1_PIN | LEFT_IN2_PIN;
#endif

#if (MOTOR_TEST_USE_RIGHT_MOTOR != 0)
    /* 右电机使能：按请求的方向计算 AIN1/AIN2 */
    motor_dir_bits(right, RIGHT_IN1_PIN, RIGHT_IN2_PIN, MOTOR_TEST_RIGHT_DIR_INVERTED,
                   &right_set, &right_reset);
#else
    /* 右电机停用：方向线固定拉低，输出关闭 */
    (void)right;
    right_reset = (uint32_t)RIGHT_IN1_PIN | RIGHT_IN2_PIN;
#endif

    set = left_set | right_set;
    reset = left_reset | right_reset;

    /*
     * BSRR 低 16 位置位、高 16 位复位；一次写入让左右方向线同时生效，
     * 不会出现左电机已换向、右电机还没换向的中间状态。
     */
    GPIOB->BSRR = set | (reset << 16U);
}

void MotorTest_SetDutyPercent(uint8_t left_percent, uint8_t right_percent)
{
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
     * 被停用的电机强制写 0，防止它误转。
     */
#if (MOTOR_TEST_USE_LEFT_MOTOR != 0)
    TIM1->CCR4 = (s_pwm_period_counts * left_percent) / 100U;
#else
    TIM1->CCR4 = 0U;
#endif

#if (MOTOR_TEST_USE_RIGHT_MOTOR != 0)
    TIM1->CCR1 = (s_pwm_period_counts * right_percent) / 100U;
#else
    TIM1->CCR1 = 0U;
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
