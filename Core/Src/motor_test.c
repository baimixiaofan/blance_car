#include "motor_test.h"

static Motor *s_left;
static Motor *s_right;

static void motor_test_apply(MotorDir dir, uint8_t duty_percent)
{
    Motor_SetDirSpeed(s_left, dir, duty_percent);
    Motor_SetDirSpeed(s_right, dir, duty_percent);
}

void MotorTest_Init(void)
{
    s_left = Motor_Init(MOTOR_LEFT);
    s_right = Motor_Init(MOTOR_RIGHT);

    if ((s_left == NULL) || (s_right == NULL))
    {
        Error_Handler();
    }

    Motor_Stop(s_left);
    Motor_Stop(s_right);
}

#if (MOTOR_TEST_MODE == 0)

typedef struct
{
    MotorDir direction;
    uint8_t duty_percent;
} MotorTestStep;

static const MotorTestStep s_steps[] =
{
    {MOTOR_DIR_FORWARD, MOTOR_TEST_DUTY_1_PERCENT},
    {MOTOR_DIR_FORWARD, MOTOR_TEST_DUTY_2_PERCENT},
    {MOTOR_DIR_FORWARD, MOTOR_TEST_DUTY_3_PERCENT},
    {MOTOR_DIR_REVERSE, MOTOR_TEST_DUTY_1_PERCENT},
    {MOTOR_DIR_REVERSE, MOTOR_TEST_DUTY_2_PERCENT},
    {MOTOR_DIR_REVERSE, MOTOR_TEST_DUTY_3_PERCENT},
};

static uint8_t s_started;        /* 是否已经过了上电延时 */
static uint8_t s_phase;          /* 0=间隙，1=输出中 */
static uint8_t s_step_index;     /* 当前执行到 s_steps 的哪一档 */
static uint32_t s_phase_start_ms;

void MotorTest_Task(void)
{
    uint32_t now = HAL_GetTick();

    (void)Motor_Update(s_left);
    (void)Motor_Update(s_right);

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
        if ((now - s_phase_start_ms) >= MOTOR_TEST_STEP_GAP_MS)
        {
            const MotorTestStep *step = &s_steps[s_step_index];
            motor_test_apply(step->direction, step->duty_percent);
            s_phase = 1U;
            s_phase_start_ms = now;
        }
    }
    else
    {
        if ((now - s_phase_start_ms) >= MOTOR_TEST_STEP_MS)
        {
            Motor_SetSpeed(s_left, 0U);
            Motor_SetSpeed(s_right, 0U);
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

static uint8_t s_fixed_applied;

void MotorTest_Task(void)
{
    (void)Motor_Update(s_left);
    (void)Motor_Update(s_right);

    if (s_fixed_applied == 0U)
    {
        if (HAL_GetTick() < MOTOR_TEST_START_DELAY_MS)
        {
            return;
        }
        Motor_SetDirSpeed(s_left, MOTOR_TEST_FIXED_LEFT_DIR,
                          MOTOR_TEST_FIXED_LEFT_DUTY);
        Motor_SetDirSpeed(s_right, MOTOR_TEST_FIXED_RIGHT_DIR,
                          MOTOR_TEST_FIXED_RIGHT_DUTY);
        s_fixed_applied = 1U;
    }
}

#endif
