#include "motor.h"

/* CubeMX 在 main.c 中定义的定时器句柄 */
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim4;

struct Motor
{
    MotorId id;
    TIM_HandleTypeDef *pwm_tim;
    uint32_t pwm_channel;

    GPIO_TypeDef *dir_port;
    uint16_t in1_pin;
    uint16_t in2_pin;
    uint8_t dir_inverted;

    TIM_HandleTypeDef *enc_tim;
    uint8_t enc_inverted;
    int32_t counts_per_rev;

    int32_t total_counts;
    uint16_t last_raw;
    uint8_t initialized;

    uint8_t move_active;
    MotorStatus move_status;
    int32_t move_start_counts;
    int32_t move_target_counts;
    MotorDir move_dir;
    uint32_t move_start_ms;
    uint32_t move_timeout_ms;
};

static Motor s_motors[MOTOR_ID_COUNT] =
{
    {
        .id = MOTOR_LEFT,
        .pwm_tim = &htim1,
        .pwm_channel = TIM_CHANNEL_1,
        .dir_port = BIN1_GPIO_Port,
        .in1_pin = BIN1_Pin,
        .in2_pin = BIN2_Pin,
        .dir_inverted = MOTOR_LEFT_DIR_INVERTED,
        .enc_tim = &htim4,
        .enc_inverted = MOTOR_LEFT_ENC_INVERTED,
        .counts_per_rev = MOTOR_COUNTS_PER_REV,
    },
    {
        .id = MOTOR_RIGHT,
        .pwm_tim = &htim1,
        .pwm_channel = TIM_CHANNEL_4,
        .dir_port = AIN1_GPIO_Port,
        .in1_pin = AIN1_Pin,
        .in2_pin = AIN2_Pin,
        .dir_inverted = MOTOR_RIGHT_DIR_INVERTED,
        .enc_tim = &htim2,
        .enc_inverted = MOTOR_RIGHT_ENC_INVERTED,
        .counts_per_rev = MOTOR_COUNTS_PER_REV,
    },
};

static void apply_direction(Motor *motor, MotorDir dir)
{
    if (motor->dir_inverted != 0U)
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
        case MOTOR_DIR_FORWARD:
            HAL_GPIO_WritePin(motor->dir_port, motor->in1_pin, GPIO_PIN_SET);
            HAL_GPIO_WritePin(motor->dir_port, motor->in2_pin, GPIO_PIN_RESET);
            break;
        case MOTOR_DIR_REVERSE:
            HAL_GPIO_WritePin(motor->dir_port, motor->in1_pin, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(motor->dir_port, motor->in2_pin, GPIO_PIN_SET);
            break;
        case MOTOR_DIR_BRAKE:
            HAL_GPIO_WritePin(motor->dir_port, motor->in1_pin, GPIO_PIN_SET);
            HAL_GPIO_WritePin(motor->dir_port, motor->in2_pin, GPIO_PIN_SET);
            break;
        case MOTOR_DIR_STOP:
        default:
            HAL_GPIO_WritePin(motor->dir_port, motor->in1_pin, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(motor->dir_port, motor->in2_pin, GPIO_PIN_RESET);
            break;
    }
}

static void finish_move(Motor *motor, MotorStatus status)
{
    Motor_SetSpeed(motor, 0U);
    apply_direction(motor, MOTOR_DIR_STOP);
    motor->move_active = 0U;
    motor->move_status = status;
}

Motor *Motor_Init(MotorId id)
{
    Motor *motor;

    if ((uint32_t)id >= (uint32_t)MOTOR_ID_COUNT)
    {
        return NULL;
    }

    motor = &s_motors[id];
    if (motor->initialized != 0U)
    {
        return motor;
    }

    motor->total_counts = 0;
    motor->last_raw = 0U;
    motor->initialized = 0U;
    motor->move_active = 0U;
    motor->move_status = MOTOR_OK;
    motor->move_start_counts = 0;
    motor->move_target_counts = 0;
    motor->move_dir = MOTOR_DIR_STOP;
    motor->move_start_ms = 0U;
    motor->move_timeout_ms = 0U;

    apply_direction(motor, MOTOR_DIR_STOP);
    Motor_SetSpeed(motor, 0U);

    if (HAL_TIM_PWM_Start(motor->pwm_tim, motor->pwm_channel) != HAL_OK)
    {
        Error_Handler();
    }

    __HAL_TIM_SET_COUNTER(motor->enc_tim, 0U);
    if (HAL_TIM_Encoder_Start(motor->enc_tim, TIM_CHANNEL_ALL) != HAL_OK)
    {
        Error_Handler();
    }
    motor->last_raw = (uint16_t)__HAL_TIM_GET_COUNTER(motor->enc_tim);

    motor->initialized = 1U;
    return motor;
}

void Motor_SetDirection(Motor *motor, MotorDir dir)
{
    if (motor == NULL)
    {
        return;
    }
    apply_direction(motor, dir);
}

void Motor_SetSpeed(Motor *motor, uint8_t percent)
{
    uint32_t period_counts;

    if (motor == NULL)
    {
        return;
    }

    if (percent > 100U)
    {
        percent = 100U;
    }

    period_counts = __HAL_TIM_GET_AUTORELOAD(motor->pwm_tim) + 1U;
    __HAL_TIM_SET_COMPARE(motor->pwm_tim, motor->pwm_channel,
                          (period_counts * (uint32_t)percent) / 100U);
}

void Motor_SetDirSpeed(Motor *motor, MotorDir dir, uint8_t percent)
{
    if (motor == NULL)
    {
        return;
    }

    Motor_SetSpeed(motor, 0U);
    apply_direction(motor, dir);
    Motor_SetSpeed(motor, percent);
}

void Motor_Stop(Motor *motor)
{
    if (motor == NULL)
    {
        return;
    }

    Motor_SetSpeed(motor, 0U);
    apply_direction(motor, MOTOR_DIR_STOP);
    motor->move_active = 0U;
    motor->move_status = MOTOR_OK;
}

void Motor_Brake(Motor *motor)
{
    if (motor == NULL)
    {
        return;
    }

    motor->move_active = 0U;
    motor->move_status = MOTOR_OK;
    apply_direction(motor, MOTOR_DIR_BRAKE);
}

MotorStatus Motor_MoveTurns(Motor *motor, uint8_t speed_percent, MotorDir dir,
                            float turns, uint32_t timeout_ms)
{
    int32_t target_counts;

    if ((motor == NULL) ||
        (speed_percent > 100U) ||
        ((dir != MOTOR_DIR_FORWARD) && (dir != MOTOR_DIR_REVERSE)) ||
        (turns <= 0.0f) ||
        (turns > 100000.0f))
    {
        return MOTOR_ERR_PARAM;
    }

    if (motor->move_active != 0U)
    {
        return MOTOR_ERR_BUSY;
    }

    (void)Motor_Update(motor);

    target_counts = (int32_t)(turns * (float)motor->counts_per_rev + 0.5f);
    if (target_counts <= 0)
    {
        return MOTOR_ERR_PARAM;
    }

    motor->move_start_counts = motor->total_counts;
    motor->move_target_counts = target_counts;
    motor->move_dir = dir;
    motor->move_start_ms = HAL_GetTick();
    motor->move_timeout_ms = timeout_ms;
    motor->move_status = MOTOR_BUSY;
    motor->move_active = 1U;

    Motor_SetDirSpeed(motor, dir, speed_percent);
    return MOTOR_BUSY;
}

MotorStatus Motor_RotateTurns(Motor *motor, uint8_t speed_percent, MotorDir dir,
                              float turns, uint32_t timeout_ms)
{
    MotorStatus status = Motor_MoveTurns(motor, speed_percent, dir, turns, timeout_ms);

    if (status != MOTOR_BUSY)
    {
        return status;
    }

    while (Motor_IsBusy(motor) != 0U)
    {
        status = Motor_Update(motor);
        if (status == MOTOR_TIMEOUT)
        {
            return status;
        }
        HAL_Delay(1U);
    }

    return Motor_GetStatus(motor);
}

MotorStatus Motor_Move(Motor *motor, uint8_t speed_percent, MotorDir dir, float turns)
{
    return Motor_MoveTurns(motor, speed_percent, dir, turns, MOTOR_DEFAULT_TIMEOUT_MS);
}

MotorStatus Motor_Rotate(Motor *motor, uint8_t speed_percent, MotorDir dir, float turns)
{
    return Motor_RotateTurns(motor, speed_percent, dir, turns, MOTOR_DEFAULT_TIMEOUT_MS);
}

MotorStatus Motor_Update(Motor *motor)
{
    uint16_t raw;
    int16_t delta;
    int32_t progress;

    if ((motor == NULL) || (motor->initialized == 0U))
    {
        return MOTOR_ERR_PARAM;
    }

    raw = (uint16_t)__HAL_TIM_GET_COUNTER(motor->enc_tim);
    delta = (int16_t)(raw - motor->last_raw);
    motor->last_raw = raw;

    if (motor->enc_inverted != 0U)
    {
        delta = (int16_t)(-delta);
    }
    motor->total_counts += (int32_t)delta;

    if (motor->move_active == 0U)
    {
        return motor->move_status;
    }

    progress = (motor->total_counts - motor->move_start_counts) *
               ((motor->move_dir == MOTOR_DIR_FORWARD) ? 1 : -1);

    if (progress >= motor->move_target_counts)
    {
        finish_move(motor, MOTOR_OK);
    }
    else if ((motor->move_timeout_ms != 0U) &&
             ((HAL_GetTick() - motor->move_start_ms) >= motor->move_timeout_ms))
    {
        finish_move(motor, MOTOR_TIMEOUT);
    }

    return motor->move_status;
}

uint8_t Motor_IsBusy(const Motor *motor)
{
    if (motor == NULL)
    {
        return 0U;
    }
    return motor->move_active;
}

MotorStatus Motor_GetStatus(const Motor *motor)
{
    if (motor == NULL)
    {
        return MOTOR_ERR_PARAM;
    }
    return motor->move_status;
}

MotorId Motor_GetId(const Motor *motor)
{
    if (motor == NULL)
    {
        return MOTOR_ID_COUNT;
    }
    return motor->id;
}

int32_t Motor_GetCount(const Motor *motor)
{
    if (motor == NULL)
    {
        return 0;
    }
    return motor->total_counts;
}

uint16_t Motor_GetRawCounter(const Motor *motor)
{
    if ((motor == NULL) || (motor->initialized == 0U))
    {
        return 0U;
    }
    return (uint16_t)__HAL_TIM_GET_COUNTER(motor->enc_tim);
}

float Motor_GetTurns(const Motor *motor)
{
    if ((motor == NULL) || (motor->counts_per_rev == 0))
    {
        return 0.0f;
    }
    return (float)motor->total_counts / (float)motor->counts_per_rev;
}

void Motor_ResetCount(Motor *motor)
{
    if (motor == NULL)
    {
        return;
    }

    Motor_Stop(motor);
    motor->total_counts = 0;
    __HAL_TIM_SET_COUNTER(motor->enc_tim, 0U);
    motor->last_raw = 0U;
    motor->move_status = MOTOR_OK;
}
