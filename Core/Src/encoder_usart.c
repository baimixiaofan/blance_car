#include "encoder_usart.h"
#include "motor.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart3;

static Motor *s_left;
static Motor *s_right;

static uint32_t s_last_sample_ms;
static uint8_t s_header_printed;
static int32_t s_last_left_count;
static int32_t s_last_right_count;
static int32_t s_last_printed_left;
static int32_t s_last_printed_right;

static void encoder_usart_print(const char *fmt, ...)
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

static void encoder_usart_print_sample(uint32_t now, uint32_t elapsed,
                                       int32_t left_count, int32_t right_count,
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

    rev_left_x100 = (int32_t)(((int64_t)left_count * 100) / MOTOR_COUNTS_PER_REV);
    rev_right_x100 = (int32_t)(((int64_t)right_count * 100) / MOTOR_COUNTS_PER_REV);

    angle_left_x100 = (int32_t)(((int64_t)abs32(left_count) * 36000) /
                                MOTOR_COUNTS_PER_REV);
    angle_right_x100 = (int32_t)(((int64_t)abs32(right_count) * 36000) /
                                 MOTOR_COUNTS_PER_REV);

    rpm_left_x10 = (int32_t)(((int64_t)left_delta * 600000) /
                             ((int64_t)MOTOR_COUNTS_PER_REV * (int64_t)elapsed));
    rpm_right_x10 = (int32_t)(((int64_t)right_delta * 600000) /
                              ((int64_t)MOTOR_COUNTS_PER_REV * (int64_t)elapsed));

    rev_l_sign = (rev_left_x100 < 0) ? '-' : '+';
    rev_r_sign = (rev_right_x100 < 0) ? '-' : '+';
    angle_l_sign = (left_count < 0) ? '-' : '+';
    angle_r_sign = (right_count < 0) ? '-' : '+';
    rpm_l_sign = (rpm_left_x10 < 0) ? '-' : '+';
    rpm_r_sign = (rpm_right_x10 < 0) ? '-' : '+';

    rev_left_x100 = abs32(rev_left_x100);
    rev_right_x100 = abs32(rev_right_x100);
    rpm_left_x10 = abs32(rpm_left_x10);
    rpm_right_x10 = abs32(rpm_right_x10);

    encoder_usart_print("T=%lums | L d=%+ld tot=%+ld rev=%c%ld.%02ld ang=%c%ld.%02ld rpm=%c%ld.%ld"
                        " | R d=%+ld tot=%+ld rev=%c%ld.%02ld ang=%c%ld.%02ld rpm=%c%ld.%ld\r\n",
                        (unsigned long)now,
                        (long)left_delta, (long)left_count,
                        rev_l_sign, (long)(rev_left_x100 / 100), (long)(rev_left_x100 % 100),
                        angle_l_sign, (long)(angle_left_x100 / 100), (long)(angle_left_x100 % 100),
                        rpm_l_sign, (long)(rpm_left_x10 / 10), (long)(rpm_left_x10 % 10),
                        (long)right_delta, (long)right_count,
                        rev_r_sign, (long)(rev_right_x100 / 100), (long)(rev_right_x100 % 100),
                        angle_r_sign, (long)(angle_right_x100 / 100), (long)(angle_right_x100 % 100),
                        rpm_r_sign, (long)(rpm_right_x10 / 10), (long)(rpm_right_x10 % 10));
}

void EncoderUsart_Init(Motor *motor)
{
    MotorId id;

    if (motor == NULL)
    {
        return;
    }

    id = Motor_GetId(motor);
    if ((id != MOTOR_LEFT) && (id != MOTOR_RIGHT))
    {
        return;
    }

    if (s_header_printed == 0U)
    {
        encoder_usart_print("\r\n=== Encoder USART ===\r\n");
        encoder_usart_print("USART1 PA9/PA10 + USART3 PB10/PB11, 115200 8N1\r\n");
        s_header_printed = 1U;
    }

    if (id == MOTOR_LEFT)
    {
        s_left = motor;
        s_last_left_count = Motor_GetCount(motor);
        s_last_printed_left = s_last_left_count;
        encoder_usart_print("LEFT  ready: TIM4 (PB6/PB7), %ld counts/rev\r\n",
                            (long)MOTOR_COUNTS_PER_REV);
    }
    else
    {
        s_right = motor;
        s_last_right_count = Motor_GetCount(motor);
        s_last_printed_right = s_last_right_count;
        encoder_usart_print("RIGHT ready: TIM2 (PA0/PA1), %ld counts/rev\r\n",
                            (long)MOTOR_COUNTS_PER_REV);
    }

    s_last_sample_ms = HAL_GetTick();
}

void EncoderUsart_Task(void)
{
    uint32_t now;
    uint32_t elapsed;
    int32_t left_count;
    int32_t right_count;
    int32_t left_delta;
    int32_t right_delta;

    if ((s_left == NULL) || (s_right == NULL))
    {
        return;
    }

    now = HAL_GetTick();
    if ((now - s_last_sample_ms) < ENCODER_USART_PERIOD_MS)
    {
        return;
    }

    elapsed = now - s_last_sample_ms;
    s_last_sample_ms = now;

    left_count = Motor_GetCount(s_left);
    right_count = Motor_GetCount(s_right);

    left_delta = left_count - s_last_left_count;
    right_delta = right_count - s_last_right_count;
    s_last_left_count = left_count;
    s_last_right_count = right_count;

#if (ENCODER_USART_ALWAYS_PRINT == 0)
    if ((left_count == s_last_printed_left) &&
        (right_count == s_last_printed_right))
    {
        return;
    }
#endif

    encoder_usart_print_sample(now, elapsed, left_count, right_count,
                               left_delta, right_delta);

    s_last_printed_left = left_count;
    s_last_printed_right = right_count;
}
