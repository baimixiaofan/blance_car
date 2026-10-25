# STM32F103C8T6 + TB6612 电机测试程序说明

> 当前固件默认 `TEST_SELECT = 4`（四圈往返），详细操作见 `HW3_GUIDE.md`。  
> 电机模块的完整接口、参数和返回值说明见 `doc/motor_api.md`。  
> 本文描述作业二的电机 PWM 测试逻辑；把 `Core/Inc/app_config.h` 里的 `TEST_SELECT` 改为 **2**，重新编译并烧录即可。

本文只说明两部分：引脚配置和程序逻辑。

---

## 1. 引脚配置

### 1.1 引脚分配

| MCU 引脚 | 功能 | CubeMX 配置 | 标签 | 说明 |
|---|---|---|---|---|
| PA8 | TIM1_CH1 | AF Push-Pull，High speed | `PWMA` | 右电机 PWM |
| PA11 | TIM1_CH4 | AF Push-Pull，High speed | `PWMB` | 左电机 PWM |
| PB12 | GPIO 输出 | Push-Pull，初始低 | `AIN1` | 右电机方向 1 |
| PB13 | GPIO 输出 | Push-Pull，初始低 | `AIN2` | 右电机方向 2 |
| PB14 | GPIO 输出 | Push-Pull，初始低 | `BIN1` | 左电机方向 1 |
| PB15 | GPIO 输出 | Push-Pull，初始低 | `BIN2` | 左电机方向 2 |
| PA13 / PA14 | SWD | Serial Wire | — | 下载调试，不占用为 GPIO |

电机归属：

| 电机 | PWM | 方向 |
|---|---|---|
| 右电机 | PA8 / TIM1_CH1 | PB12 / PB13 |
| 左电机 | PA11 / TIM1_CH4 | PB14 / PB15 |

### 1.2 TIM1 配置

| 参数 | 值 |
|---|---|
| Prescaler | 0 |
| Counter Period（ARR） | 399 |
| Counter Mode | 向上计数 |
| PWM Mode | PWM Mode 1 |
| PWM 通道 | CH1（PA8）、CH4（PA11） |
| 输出极性 | 高电平有效 |
| 初始 Pulse | 0 |

8 MHz 时钟下：

```text
PWM 频率 = 8 MHz / (399 + 1) = 20 kHz
```

### 1.3 GPIO 初始状态

- PB12~PB15 上电配置为推挽输出，初始电平全低，保证上电时电机不转。
- PA8、PA11 由 TIM1 复用输出，初始占空比为 0。
- 左右电机默认都使能。编码器手动计圈模式（`TEST_SELECT=0`）下，程序会把方向线置停止、占空比清零，电机不会转动。

---

## 2. 程序逻辑

### 2.1 初始化流程

```text
HAL_Init()
    ↓
SystemClock_Config()     // HSI 8 MHz
    ↓
MX_GPIO_Init()           // PB12~PB15 方向线，初始低
    ↓
MX_TIM1_Init()           // TIM1 20 kHz，PA8/PA11 复用输出
    ↓
MotorTest_Init()         // 取得左右 Motor 指针，方向停止、占空比 0、启动 PWM/编码器
    ↓
while (1)
    MotorTest_Task()     // 测试流程
    HAL_Delay(5)
```

各部分职责：

| 代码 | 职责 |
|---|---|
| `MX_GPIO_Init()` | CubeMX 生成，配置方向线 GPIO |
| `MX_TIM1_Init()` | CubeMX 生成，配置 TIM1 和 PA8/PA11 复用功能 |
| `MotorTest_Init()` | 调用 `Motor_Init()` 取得左右电机指针，并让两路进入停止状态 |
| `MotorTest_Task()` | 周期执行，负责上电延时、换档、换向 |
| `Motor_SetSpeed()` | 按百分比换算 CCR，写入该电机对应的 PWM 通道 |
| `Motor_SetDirection()` | 按方向写该电机的 IN1/IN2 |
| `Motor_SetDirSpeed()` | 先清零占空比、再换向、最后给新占空比 |

`MotorTest_Init()` 的处理顺序：

1. `Motor_Init(MOTOR_LEFT)` / `Motor_Init(MOTOR_RIGHT)` 返回两个电机指针；
2. 方向线全部置低（停止）；
3. 左右占空比清零；
4. 启动 TIM1 的 CH4、CH1 以及 TIM4、TIM2 编码器接口。

### 2.2 方向控制逻辑

TB6612 方向真值表：

| IN1 | IN2 | 状态 |
|---|---|---|
| 1 | 0 | 正转 |
| 0 | 1 | 反转 |
| 0 | 0 | 停止 |
| 1 | 1 | 刹车 |

程序中的方向枚举：

| 枚举 | 行为 |
|---|---|
| `MOTOR_DIR_FORWARD` | IN1=1，IN2=0 |
| `MOTOR_DIR_REVERSE` | IN1=0，IN2=1 |
| `MOTOR_DIR_STOP` | IN1=IN2=0 |
| `MOTOR_DIR_BRAKE` | IN1=IN2=1 |

如果实际电机转向与标注相反，可在 `Core/Inc/motor.h` 把对应宏改为 1：

```c
#define MOTOR_LEFT_DIR_INVERTED     1
#define MOTOR_RIGHT_DIR_INVERTED    1
```

该宏只交换正转和反转时 IN1/IN2 的电平，不影响停止和刹车。

### 2.3 占空比控制逻辑

左右占空比通过修改 TIM1 的 CCR 实现：

```text
CCR = (ARR + 1) × 占空比 / 100
```

- ARR 从 `htim1` 当前配置读取，因此 CubeMX 中修改 ARR 后代码会自动适配。
- 左电机使用 `TIM_CHANNEL_4`，右电机使用 `TIM_CHANNEL_1`。
- CCR 预装载已使能，新占空比在下一个更新事件生效。
- 占空比范围 0~100，超过 100 按 100 处理。

当前默认参数下：

| 占空比 | CCR |
|---:|---:|
| 0% | 0 |
| 30% | 120 |
| 60% | 240 |
| 90% | 360 |

### 2.4 自动测试状态机

`MOTOR_TEST_MODE = 0` 时进入自动模式，`MotorTest_Task()` 维护两个阶段：

| 阶段 | 行为 |
|---|---|
| `s_phase = 0` | 停止间隙：PWM 占空比为 0，等待 `MOTOR_TEST_STEP_GAP_MS` |
| `s_phase = 1` | 输出阶段：等待 `MOTOR_TEST_STEP_MS`，到时清零占空比并切到下一档 |

切换档位的顺序：

```text
正转 30% → 正转 60% → 正转 90%
→ 反转 30% → 反转 60% → 反转 90%
→ 回到正转 30%，循环
```

默认时间参数：

- 上电延时：1000 ms
- 每档保持：5000 ms
- 档间停止：500 ms

对应时间表：

| 相对上电时间 | 方向 | 占空比 |
|---:|---|---:|
| 0 ~ 1.5 s | 停止 | 0% |
| 1.5 ~ 6.5 s | 正转 | 30% |
| 7.0 ~ 12.0 s | 正转 | 60% |
| 12.5 ~ 17.5 s | 正转 | 90% |
| 18.0 ~ 23.0 s | 反转 | 30% |
| 23.5 ~ 28.5 s | 反转 | 60% |
| 29.0 ~ 34.0 s | 反转 | 90% |
| 34.5 s 起 | 重复循环 | — |

换向安全处理：

- 每档结束时先把占空比清零；
- 经过 0.5 s 停止间隙后才改变方向线；
- 避免电机带载时瞬间反接产生大电流。

### 2.5 固定模式

`MOTOR_TEST_MODE = 1` 时进入固定模式：

1. 上电等待 `MOTOR_TEST_START_DELAY_MS`；
2. 应用一次固定的方向和占空比；
3. 之后保持不变，适合示波器拍照。

示例：右电机反转 60%，左电机停止：

```c
// motor_test.h：固定模式参数
#define MOTOR_TEST_MODE                1
#define MOTOR_TEST_FIXED_LEFT_DIR      MOTOR_DIR_STOP
#define MOTOR_TEST_FIXED_RIGHT_DIR     MOTOR_DIR_REVERSE
#define MOTOR_TEST_FIXED_LEFT_DUTY     0U
#define MOTOR_TEST_FIXED_RIGHT_DUTY    60U
```

左右两路始终都会被初始化，需要停哪一路就把它的方向设为 `MOTOR_DIR_STOP`、占空比设为 `0`。

### 2.6 电机对象与转圈 API

所有电机和编码器操作都在 `Core/Src/motor.c` / `Core/Inc/motor.h`：

```c
Motor *left  = Motor_Init(MOTOR_LEFT);
Motor *right = Motor_Init(MOTOR_RIGHT);

Motor_SetDirSpeed(left, MOTOR_DIR_FORWARD, 50);   // 左轮 50% 正转
Motor_Stop(left);                                  // 停止
```

转动指定圈数（圈数指车轮/输出轴圈数，1560 计数 = 1 圈）：

```c
// 阻塞版：函数返回时已经转完；只适合单电机
MotorStatus st = Motor_Rotate(left, 50, MOTOR_DIR_FORWARD, 4.0f);

// 非阻塞版：立即返回，主循环持续调用 Motor_Update()
Motor_Move(left, 50, MOTOR_DIR_FORWARD, 4.0f);
while (Motor_IsBusy(left)) {
    Motor_Update(left);
    HAL_Delay(1);
}
```

默认超时 5 s；需要自定义超时用 `Motor_RotateTurns()` / `Motor_MoveTurns()` 的最后一个参数。

`Motor_Update()` 同时负责读取编码器、累计计数、判断是否到达目标圈数和超时。超时后会自动停车，返回 `MOTOR_TIMEOUT`。

### 2.7 主要参数

| 宏 | 默认值 | 作用 |
|---|---:|---|
| `MOTOR_TEST_MODE` | 0 | 0=自动循环，1=固定状态 |
| `MOTOR_TEST_STEP_MS` | 5000 | 每档保持时间 |
| `MOTOR_TEST_STEP_GAP_MS` | 500 | 档间停止时间 |
| `MOTOR_TEST_START_DELAY_MS` | 1000 | 上电后延时 |
| `MOTOR_TEST_DUTY_1_PERCENT` | 30 | 第一档占空比 |
| `MOTOR_TEST_DUTY_2_PERCENT` | 60 | 第二档占空比 |
| `MOTOR_TEST_DUTY_3_PERCENT` | 90 | 第三档占空比 |
| `MOTOR_TEST_FIXED_LEFT_DIR` | FORWARD | 固定模式左电机方向 |
| `MOTOR_TEST_FIXED_RIGHT_DIR` | FORWARD | 固定模式右电机方向 |
| `MOTOR_TEST_FIXED_LEFT_DUTY` | 60 | 固定模式左电机占空比 |
| `MOTOR_TEST_FIXED_RIGHT_DUTY` | 60 | 固定模式右电机占空比 |

方向反接修正、编码器计数方向修正都在 `motor.h`：

| 宏 | 默认值 | 作用 |
|---|---:|---|
| `MOTOR_LEFT_DIR_INVERTED` | 0 | 左电机方向反接修正 |
| `MOTOR_RIGHT_DIR_INVERTED` | 0 | 右电机方向反接修正 |
| `MOTOR_LEFT_ENC_INVERTED` | 0 | 左编码器计数方向修正 |
| `MOTOR_RIGHT_ENC_INVERTED` | 0 | 右编码器计数方向修正 |

### 2.8 对外接口

| 函数 | 作用 |
|---|---|
| `MotorTest_Init()` | 初始化左右电机并进入停止状态 |
| `MotorTest_Task()` | 周期运行测试状态机 |

电机底层接口在 `motor.h`：

| 函数 | 作用 |
|---|---|
| `Motor_Init(id)` | 初始化并返回电机指针 |
| `Motor_SetDirection(motor, dir)` | 设置方向 |
| `Motor_SetSpeed(motor, percent)` | 设置占空比（0~100） |
| `Motor_SetDirSpeed(motor, dir, percent)` | 安全换向并设置占空比 |
| `Motor_Stop(motor)` | 停止并取消未完成的转动 |
| `Motor_MoveTurns(...)` | 非阻塞转动指定圈数（自定义超时） |
| `Motor_RotateTurns(...)` | 阻塞转动指定圈数（自定义超时） |
| `Motor_Move(motor, speed, dir, turns)` | 非阻塞转动，默认 5 s 超时 |
| `Motor_Rotate(motor, speed, dir, turns)` | 阻塞转动，默认 5 s 超时 |
| `Motor_Update(motor)` | 读编码器并推进转动状态 |

`MotorTest_Init()` 必须在 CubeMX 生成的 `MX_GPIO_Init()`、`MX_TIM1_Init()` 之后调用。
