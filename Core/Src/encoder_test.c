/*
 * 编码器计圈/测速测试
 *
 * 编码器读取、累计和电机转动都在 motor.c 里。本文件只做：
 *   - 按 TEST_SELECT 选择测试流程
 *   - 每 100 ms 采样并打印
 *   - 四圈往返的状态机
 *
 * 串口：USART1（PA9=TX，PA10=RX）为主，USB-TTL 接这里；
 *       同一份数据也输出到 USART3（PB10=TX，PB11=RX）。
 *       两个串口均为 115200 8N1。
 */

#include "encoder_test.h"
#include "motor.h"
#include <stdio.h>
#include <stdarg.h>

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart3;

static Motor *s_left;
static Motor *s_right;

static int32_t s_total_left;
static int32_t s_total_right;
static int32_t s_last_printed_left;
static int32_t s_last_printed_right;
static uint32_t s_last_sample_ms;

#if (ENCODER_TEST_MODE == 1)
static uint8_t s_motor_started;
static uint32_t s_boot_ms;
#elif (ENCODER_TEST_MODE == 2)
static uint8_t s_debug_started;
static uint32_t s_debug_start_ms;
#elif (ENCODER_TEST_MODE == 3)
static uint8_t s_four_phase;          /* 0=等待,1=正转,2=停止间隙,3=反转,4=完成 */
static uint32_t s_four_start_ms;
static uint32_t s_four_phase_ms;
#endif

static void encoder_print(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    int len;

    va_start(args, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len > 0)
    {
        if (len >= (int)sizeof(buf))
        {
            len = (int)sizeof(buf) - 1;
        }

        HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)len, 100U);
        HAL_UART_Transmit(&huart3, (uint8_t *)buf, (uint16_t)len, 100U);
    }
}

static int32_t abs32(int32_t value)
{
    return (value < 0) ? -value : value;
}

/* 用 Motor 的累计计数计算本次采样增量。 */
static void encoder_read_delta(int32_t *left_delta, int32_t *right_delta)
{
    int32_t left_now = Motor_GetCount(s_left);
    int32_t right_now = Motor_GetCount(s_right);

    *left_delta = left_now - s_total_left;
    *right_delta = right_now - s_total_right;

    s_total_left = left_now;
    s_total_right = right_now;
}

#if (ENCODER_TEST_MODE == 0)
static void encoder_reset_counters(void)
{
    Motor_ResetCount(s_left);
    Motor_ResetCount(s_right);

    s_total_left = 0;
    s_total_right = 0;
    s_last_printed_left = 0;
    s_last_printed_right = 0;
    s_last_sample_ms = HAL_GetTick();
}
#endif

#if (ENCODER_TEST_MODE != 2)
static void encoder_print_sample(uint32_t now, uint32_t elapsed,
                                 int32_t left_delta, int32_t right_delta)
{
    int32_t rev_left_x100;
    int32_t rev_right_x100;
    int32_t angle_left_x100;
    int32_t angle_right_x100;
    int32_t rpm_left_x10;
    int32_t rpm_right_x10;
    char rev_l_sign;
    char rev_r_sign;
    char angle_l_sign;
    char angle_r_sign;
    char rpm_l_sign;
    char rpm_r_sign;

    rev_left_x100 = (int32_t)(((int64_t)s_total_left * 100) / MOTOR_COUNTS_PER_REV);
    rev_right_x100 = (int32_t)(((int64_t)s_total_right * 100) / MOTOR_COUNTS_PER_REV);

    angle_left_x100 = (int32_t)(((int64_t)abs32(s_total_left) * 36000) /
                                MOTOR_COUNTS_PER_REV);
    angle_right_x100 = (int32_t)(((int64_t)abs32(s_total_right) * 36000) /
                                 MOTOR_COUNTS_PER_REV);

    rpm_left_x10 = (int32_t)(((int64_t)left_delta * 600000) /
                             ((int64_t)MOTOR_COUNTS_PER_REV * (int64_t)elapsed));
    rpm_right_x10 = (int32_t)(((int64_t)right_delta * 600000) /
                              ((int64_t)MOTOR_COUNTS_PER_REV * (int64_t)elapsed));

    rev_l_sign = (rev_left_x100 < 0) ? '-' : '+';
    rev_r_sign = (rev_right_x100 < 0) ? '-' : '+';
    angle_l_sign = (s_total_left < 0) ? '-' : '+';
    angle_r_sign = (s_total_right < 0) ? '-' : '+';
    rpm_l_sign = (rpm_left_x10 < 0) ? '-' : '+';
    rpm_r_sign = (rpm_right_x10 < 0) ? '-' : '+';

    rev_left_x100 = abs32(rev_left_x100);
    rev_right_x100 = abs32(rev_right_x100);
    rpm_left_x10 = abs32(rpm_left_x10);
    rpm_right_x10 = abs32(rpm_right_x10);

    encoder_print("T=%lums | L d=%+ld tot=%+ld rev=%c%ld.%02ld ang=%c%ld.%02ld rpm=%c%ld.%ld"
                  " | R d=%+ld tot=%+ld rev=%c%ld.%02ld ang=%c%ld.%02ld rpm=%c%ld.%ld\r\n",
                  (unsigned long)now,
                  (long)left_delta, (long)s_total_left,
                  rev_l_sign, (long)(rev_left_x100 / 100), (long)(rev_left_x100 % 100),
                  angle_l_sign, (long)(angle_left_x100 / 100), (long)(angle_left_x100 % 100),
                  rpm_l_sign, (long)(rpm_left_x10 / 10), (long)(rpm_left_x10 % 10),
                  (long)right_delta, (long)s_total_right,
                  rev_r_sign, (long)(rev_right_x100 / 100), (long)(rev_right_x100 % 100),
                  angle_r_sign, (long)(angle_right_x100 / 100), (long)(angle_right_x100 % 100),
                  rpm_r_sign, (long)(rpm_right_x10 / 10), (long)(rpm_right_x10 % 10));
}
#endif

#if (ENCODER_TEST_MODE == 2)
static void encoder_print_debug_sample(uint32_t now, uint32_t elapsed,
                                       int32_t left_delta, int32_t right_delta)
{
    uint16_t raw_left = Motor_GetRawCounter(s_left);
    uint16_t raw_right = Motor_GetRawCounter(s_right);
    unsigned left_a = (unsigned)HAL_GPIO_ReadPin(ENC_LA_GPIO_Port, ENC_LA_Pin);
    unsigned left_b = (unsigned)HAL_GPIO_ReadPin(ENC_LB_GPIO_Port, ENC_LB_Pin);
    unsigned right_a = (unsigned)HAL_GPIO_ReadPin(ENC_RA_GPIO_Port, ENC_RA_Pin);
    unsigned right_b = (unsigned)HAL_GPIO_ReadPin(ENC_RB_GPIO_Port, ENC_RB_Pin);
    int32_t rev_left_x100 = (int32_t)(((int64_t)s_total_left * 100) / MOTOR_COUNTS_PER_REV);
    int32_t rev_right_x100 = (int32_t)(((int64_t)s_total_right * 100) / MOTOR_COUNTS_PER_REV);
    int32_t rpm_left_x10 = (int32_t)(((int64_t)left_delta * 600000) /
                                     ((int64_t)MOTOR_COUNTS_PER_REV * (int64_t)elapsed));
    int32_t rpm_right_x10 = (int32_t)(((int64_t)right_delta * 600000) /
                                      ((int64_t)MOTOR_COUNTS_PER_REV * (int64_t)elapsed));
    char rev_l_sign = (rev_left_x100 < 0) ? '-' : '+';
    char rev_r_sign = (rev_right_x100 < 0) ? '-' : '+';
    char rpm_l_sign = (rpm_left_x10 < 0) ? '-' : '+';
    char rpm_r_sign = (rpm_right_x10 < 0) ? '-' : '+';

    rev_left_x100 = abs32(rev_left_x100);
    rev_right_x100 = abs32(rev_right_x100);
    rpm_left_x10 = abs32(rpm_left_x10);
    rpm_right_x10 = abs32(rpm_right_x10);

    encoder_print("T=%lums dt=%lums | L A=%u B=%u cnt=%u d=%+ld tot=%+ld rev=%c%ld.%02ld rpm=%c%ld.%ld"
                  " | R A=%u B=%u cnt=%u d=%+ld tot=%+ld rev=%c%ld.%02ld rpm=%c%ld.%ld\r\n",
                  (unsigned long)now, (unsigned long)elapsed,
                  left_a, left_b, (unsigned)raw_left,
                  (long)left_delta, (long)s_total_left,
                  rev_l_sign, (long)(rev_left_x100 / 100), (long)(rev_left_x100 % 100),
                  rpm_l_sign, (long)(rpm_left_x10 / 10), (long)(rpm_left_x10 % 10),
                  right_a, right_b, (unsigned)raw_right,
                  (long)right_delta, (long)s_total_right,
                  rev_r_sign, (long)(rev_right_x100 / 100), (long)(rev_right_x100 % 100),
                  rpm_r_sign, (long)(rpm_right_x10 / 10), (long)(rpm_right_x10 % 10));
}
#endif

#if (ENCODER_TEST_MODE == 3)
#if (ENCODER_FOUR_TURN_MOTOR == 1)
#define FOUR_TURN_LEFT_EN       1U
#define FOUR_TURN_RIGHT_EN      0U
#elif (ENCODER_FOUR_TURN_MOTOR == 2)
#define FOUR_TURN_LEFT_EN       0U
#define FOUR_TURN_RIGHT_EN      1U
#else
#define FOUR_TURN_LEFT_EN       1U
#define FOUR_TURN_RIGHT_EN      1U
#endif

static uint8_t four_turn_start(MotorDir dir)
{
    uint8_t started = 1U;

#if (FOUR_TURN_LEFT_EN != 0U)
    if (Motor_MoveTurns(s_left, ENCODER_FOUR_TURN_DUTY_PERCENT, dir,
                        (float)ENCODER_FOUR_TURN_TURNS,
                        ENCODER_FOUR_TURN_TIMEOUT_MS) != MOTOR_BUSY)
    {
        started = 0U;
    }
#else
    Motor_Stop(s_left);
#endif

#if (FOUR_TURN_RIGHT_EN != 0U)
    if (Motor_MoveTurns(s_right, ENCODER_FOUR_TURN_DUTY_PERCENT, dir,
                        (float)ENCODER_FOUR_TURN_TURNS,
                        ENCODER_FOUR_TURN_TIMEOUT_MS) != MOTOR_BUSY)
    {
        started = 0U;
    }
#else
    Motor_Stop(s_right);
#endif

    if (started == 0U)
    {
        Motor_Stop(s_left);
        Motor_Stop(s_right);
    }

    return started;
}

static uint8_t four_turn_busy(void)
{
    if ((Motor_IsBusy(s_left) != 0U) || (Motor_IsBusy(s_right) != 0U))
    {
        return 1U;
    }
    return 0U;
}

static uint8_t four_turn_timeout(void)
{
#if (FOUR_TURN_LEFT_EN != 0U)
    if (Motor_GetStatus(s_left) == MOTOR_TIMEOUT)
    {
        return 1U;
    }
#endif
#if (FOUR_TURN_RIGHT_EN != 0U)
    if (Motor_GetStatus(s_right) == MOTOR_TIMEOUT)
    {
        return 1U;
    }
#endif
    return 0U;
}

static void encoder_four_turn_update(uint32_t now)
{
    switch (s_four_phase)
    {
        case 0U:
            if ((now - s_four_start_ms) < ENCODER_MOTOR_START_DELAY_MS)
            {
                return;
            }
            if (four_turn_start(MOTOR_DIR_FORWARD) == 0U)
            {
                s_four_phase = 4U;
                encoder_print("FOUR TURN: START FAILED\r\n");
                break;
            }
            s_four_phase = 1U;
            s_four_phase_ms = now;
            encoder_print("FOUR TURN: FORWARD %u rev, duty=%u%%\r\n",
                          (unsigned)ENCODER_FOUR_TURN_TURNS,
                          (unsigned)ENCODER_FOUR_TURN_DUTY_PERCENT);
            break;

        case 1U:
            if (four_turn_busy() == 0U)
            {
                if (four_turn_timeout() != 0U)
                {
                    s_four_phase = 4U;
                    encoder_print("FOUR TURN: TIMEOUT FORWARD\r\n");
                }
                else
                {
                    s_four_phase = 2U;
                    s_four_phase_ms = now;
                    encoder_print("FOUR TURN: FORWARD DONE\r\n");
                }
            }
            break;

        case 2U:
            if ((now - s_four_phase_ms) >= ENCODER_FOUR_TURN_GAP_MS)
            {
                if (four_turn_start(MOTOR_DIR_REVERSE) == 0U)
                {
                    s_four_phase = 4U;
                    encoder_print("FOUR TURN: START FAILED\r\n");
                    break;
                }
                s_four_phase = 3U;
                s_four_phase_ms = now;
                encoder_print("FOUR TURN: REVERSE %u rev\r\n",
                              (unsigned)ENCODER_FOUR_TURN_TURNS);
            }
            break;

        case 3U:
            if (four_turn_busy() == 0U)
            {
                s_four_phase = 4U;
                if (four_turn_timeout() != 0U)
                {
                    encoder_print("FOUR TURN: TIMEOUT REVERSE\r\n");
                }
                else
                {
                    encoder_print("FOUR TURN: DONE\r\n");
                }
            }
            break;

        default:
            break;
    }
}
#endif

void EncoderTest_Init(void)
{
    s_left = Motor_Init(MOTOR_LEFT);
    s_right = Motor_Init(MOTOR_RIGHT);

    if ((s_left == NULL) || (s_right == NULL))
    {
        Error_Handler();
    }

    Motor_Stop(s_left);
    Motor_Stop(s_right);

    s_total_left = 0;
    s_total_right = 0;
    s_last_printed_left = 0;
    s_last_printed_right = 0;
    s_last_sample_ms = HAL_GetTick();

#if (ENCODER_TEST_MODE == 1)
    s_boot_ms = s_last_sample_ms;
    s_motor_started = 0U;
#elif (ENCODER_TEST_MODE == 2)
    s_debug_started = 0U;
    s_debug_start_ms = s_last_sample_ms;
#elif (ENCODER_TEST_MODE == 3)
    s_four_phase = 0U;
    s_four_start_ms = s_last_sample_ms;
    s_four_phase_ms = s_last_sample_ms;
#endif

    encoder_print("\r\n=== Encoder Test ===\r\n");
    encoder_print("AB encoder: 13 PPR x 30:1 x 4 = %ld counts/rev\r\n",
                  (long)MOTOR_COUNTS_PER_REV);
    encoder_print("L=TIM4 (PB6/PB7)  R=TIM2 (PA0/PA1)  sample=%lums\r\n",
                  (unsigned long)ENCODER_SAMPLE_MS);
    encoder_print("UART: USART1 PA9/PA10 + USART3 PB10/PB11, 115200 8N1\r\n");
#if (ENCODER_TEST_MODE != 2)
    encoder_print("Serial output starts only after encoder counts change.\r\n");
#endif

#if (ENCODER_TEST_MODE == 0)
    encoder_print("MODE: manual count, motors disabled\r\n");
    encoder_print("Turn wheel by hand. Send 'r' to reset counters.\r\n");
#elif (ENCODER_TEST_MODE == 1)
#if (ENCODER_NOLOAD_MOTOR == 1)
    encoder_print("MODE: no-load test, LEFT motor only, continuous\r\n");
#elif (ENCODER_NOLOAD_MOTOR == 2)
    encoder_print("MODE: no-load test, RIGHT motor only, continuous\r\n");
#else
    encoder_print("MODE: no-load test, BOTH motors continuous\r\n");
#endif
    encoder_print("duty=%u%%  runs until reset/power off  rated=%ld RPM\r\n",
                  (unsigned)ENCODER_MOTOR_DUTY_PERCENT,
                  (long)ENCODER_MOTOR_RATED_RPM);
    encoder_print("WARNING: lift wheels off the ground\r\n");
#elif (ENCODER_TEST_MODE == 2)
    encoder_print("MODE: DEBUG - BOTH motors continuous, duty=%u%%\r\n",
                  (unsigned)ENCODER_DEBUG_DUTY_PERCENT);
    encoder_print("Prints every %lums: A/B pin levels, raw TIM counter, delta, total.\r\n",
                  (unsigned long)ENCODER_SAMPLE_MS);
    encoder_print("Motor runs until reset/power off. WARNING: lift wheels off the ground\r\n");
#else
#if ((ENCODER_FOUR_TURN_MOTOR == 1) || (ENCODER_FOUR_TURN_MOTOR == 2))
    encoder_print("MODE: FOUR TURN - %s motor, %u rev forward then %u rev reverse\r\n",
                  (ENCODER_FOUR_TURN_MOTOR == 1) ? "LEFT" : "RIGHT",
                  (unsigned)ENCODER_FOUR_TURN_TURNS,
                  (unsigned)ENCODER_FOUR_TURN_TURNS);
#else
    encoder_print("MODE: FOUR TURN - BOTH motors, %u rev forward then %u rev reverse\r\n",
                  (unsigned)ENCODER_FOUR_TURN_TURNS,
                  (unsigned)ENCODER_FOUR_TURN_TURNS);
#endif
    encoder_print("duty=%u%%  stop=%lums  timeout=%lums\r\n",
                  (unsigned)ENCODER_FOUR_TURN_DUTY_PERCENT,
                  (unsigned long)ENCODER_FOUR_TURN_GAP_MS,
                  (unsigned long)ENCODER_FOUR_TURN_TIMEOUT_MS);
    encoder_print("WARNING: lift wheels off the ground\r\n");
#endif
}

void EncoderTest_Task(void)
{
    uint32_t now = HAL_GetTick();

    (void)Motor_Update(s_left);
    (void)Motor_Update(s_right);

#if (ENCODER_TEST_MODE == 0)
    uint8_t cmd;
    uint8_t received = 0U;

    if (HAL_UART_Receive(&huart1, &cmd, 1U, 0U) == HAL_OK)
    {
        received = cmd;
    }
    else if (HAL_UART_Receive(&huart3, &cmd, 1U, 0U) == HAL_OK)
    {
        received = cmd;
    }

    if ((received == 'r') || (received == 'R'))
    {
        encoder_reset_counters();
        encoder_print("\r\nRESET: counters cleared\r\n");
    }
#elif (ENCODER_TEST_MODE == 1)
    if ((s_motor_started == 0U) &&
        ((now - s_boot_ms) >= ENCODER_MOTOR_START_DELAY_MS))
    {
#if (ENCODER_NOLOAD_MOTOR == 1)
        Motor_SetDirSpeed(s_left, MOTOR_DIR_FORWARD, ENCODER_MOTOR_DUTY_PERCENT);
        Motor_Stop(s_right);
        encoder_print("MOTOR START: LEFT forward, duty=%u%%\r\n",
                      (unsigned)ENCODER_MOTOR_DUTY_PERCENT);
#elif (ENCODER_NOLOAD_MOTOR == 2)
        Motor_SetDirSpeed(s_right, MOTOR_DIR_FORWARD, ENCODER_MOTOR_DUTY_PERCENT);
        Motor_Stop(s_left);
        encoder_print("MOTOR START: RIGHT forward, duty=%u%%\r\n",
                      (unsigned)ENCODER_MOTOR_DUTY_PERCENT);
#else
        Motor_SetDirSpeed(s_left, MOTOR_DIR_FORWARD, ENCODER_MOTOR_DUTY_PERCENT);
        Motor_SetDirSpeed(s_right, MOTOR_DIR_FORWARD, ENCODER_MOTOR_DUTY_PERCENT);
        encoder_print("MOTOR START: BOTH forward, duty=%u%%\r\n",
                      (unsigned)ENCODER_MOTOR_DUTY_PERCENT);
#endif

        s_motor_started = 1U;
    }
#elif (ENCODER_TEST_MODE == 2)
    if ((s_debug_started == 0U) &&
        ((now - s_debug_start_ms) >= ENCODER_MOTOR_START_DELAY_MS))
    {
        Motor_SetDirSpeed(s_left, MOTOR_DIR_FORWARD, ENCODER_DEBUG_DUTY_PERCENT);
        Motor_SetDirSpeed(s_right, MOTOR_DIR_FORWARD, ENCODER_DEBUG_DUTY_PERCENT);
        s_debug_started = 1U;
        encoder_print("MOTOR START: BOTH forward, duty=%u%% (continuous debug)\r\n",
                      (unsigned)ENCODER_DEBUG_DUTY_PERCENT);
    }
#endif

    if ((now - s_last_sample_ms) >= ENCODER_SAMPLE_MS)
    {
        uint32_t elapsed = now - s_last_sample_ms;
        int32_t left_delta;
        int32_t right_delta;

        s_last_sample_ms = now;
        encoder_read_delta(&left_delta, &right_delta);

#if (ENCODER_TEST_MODE == 2)
        encoder_print_debug_sample(now, elapsed, left_delta, right_delta);
#else
        if ((s_total_left != s_last_printed_left) ||
            (s_total_right != s_last_printed_right))
        {
            encoder_print_sample(now, elapsed, left_delta, right_delta);
            s_last_printed_left = s_total_left;
            s_last_printed_right = s_total_right;
        }
#endif
    }

#if (ENCODER_TEST_MODE == 3)
    encoder_four_turn_update(now);
#endif
}
