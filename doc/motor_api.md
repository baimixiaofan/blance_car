# 电机模块接口文档

> 模块文件：`Core/Inc/motor.h`、`Core/Src/motor.c`  
> 适用工程：STM32F103C8T6 + TB6612FNG + AB 增量式编码器  
> 本文档只描述电机模块本身；作业测试流程见 `README.md`、`TEST_GUIDE.md`、`HW3_GUIDE.md`。

---

## 1. 模块职责

`motor.c` 把“一个电机 + 它自己的编码器”封装成一个 `Motor` 对象，负责：

- TB6612 方向线控制
- TIM1 PWM 占空比控制
- TIM2 / TIM4 编码器读取与累计
- 按“车轮圈数”闭环转动
- 超时自动停车
- 屏蔽 16 位计数器回绕

应用层不再直接操作 GPIO、TIM 寄存器或编码器计数器，只通过 `Motor *` 指针调用接口。

---

## 2. 硬件映射

| 对象 | PWM | 方向线 | 编码器 | 每圈计数 |
|---|---|---|---|---:|
| 左电机 | PA8 / TIM1_CH1（标签 `PWMA`） | PB14 / PB15（`BIN1` / `BIN2`） | PB6(A) / PB7(B) / TIM4 | 1560 |
| 右电机 | PA11 / TIM1_CH4（标签 `PWMB`） | PB12 / PB13（`AIN1` / `AIN2`） | PA15(B) / PB3(A) / TIM2（重映射） | 1560 |

编码器换算：

```text
13 线 × 30:1 减速比 × 4 倍频 = 1560 计数/车轮圈
1 计数 ≈ 0.2308°
```

电气注意：

- PA15 / PB3 可耐 5 V，但编码器仍必须供 3.3 V；这两个脚默认是 JTAG 的 JTDI/JTDO，依赖 `SWJ_NOJTAG`。
- PA0 / PA1 已不再使用（原来做右编码器），要做 ADC 的话现在是空的。
- PB6 / PB7 可耐 5 V，但建议同样按 3.3 V 使用。
- 编码器开漏输出且计数抖动时，可在 A/B 到 3.3 V 之间加 4.7 kΩ～10 kΩ 上拉。

---

## 3. 单位与方向约定

| 概念 | 约定 |
|---|---|
| 圈数 `turns` | **车轮/输出轴圈数**，不是电机轴圈数 |
| 速度 `speed_percent` | PWM 占空比，范围 0～100，单位 % |
| 正转 | `MOTOR_DIR_FORWARD`，方向线 IN1=1、IN2=0 |
| 反转 | `MOTOR_DIR_REVERSE`，方向线 IN1=0、IN2=1 |
| 停止 | `MOTOR_DIR_STOP`，IN1=IN2=0，输出关闭 |
| 刹车 | `MOTOR_DIR_BRAKE`，IN1=IN2=1，短接制动 |
| 编码器计数 | 带符号；正常情况下正转增加，反转减少 |

方向反接或编码器计数方向相反时，不需要改接线，修改 `motor.h` 里的宏：

```c
#define MOTOR_LEFT_DIR_INVERTED     0   // 左电机方向反接改 1
#define MOTOR_RIGHT_DIR_INVERTED    0   // 右电机方向反接改 1
#define MOTOR_LEFT_ENC_INVERTED     0   // 左编码器计数方向反相改 1
#define MOTOR_RIGHT_ENC_INVERTED    0   // 右编码器计数方向反相改 1
```

> 如果 `MOTOR_*_ENC_INVERTED` 配错，闭环转圈会因为“计数方向与目标方向相反”一直达不到目标，最终返回 `MOTOR_TIMEOUT`。这是判断编码器方向是否正确的直接方法。

---

## 4. 数据类型

### 4.1 `MotorId`

```c
typedef enum
{
    MOTOR_LEFT = 0,
    MOTOR_RIGHT = 1,
    MOTOR_ID_COUNT
} MotorId;
```

| 值 | 含义 |
|---|---|
| `MOTOR_LEFT` | 左电机 + TIM4 编码器 |
| `MOTOR_RIGHT` | 右电机 + TIM2 编码器 |
| `MOTOR_ID_COUNT` | 电机数量，仅用于边界检查 |

### 4.2 `MotorDir`

```c
typedef enum
{
    MOTOR_DIR_STOP = 0,
    MOTOR_DIR_FORWARD = 1,
    MOTOR_DIR_REVERSE = 2,
    MOTOR_DIR_BRAKE = 3
} MotorDir;
```

| 枚举 | IN1 | IN2 | 行为 |
|---|---:|---:|---|
| `MOTOR_DIR_STOP` | 0 | 0 | 输出关闭，滑行 |
| `MOTOR_DIR_FORWARD` | 1 | 0 | 正转，OUT1 输出 PWM |
| `MOTOR_DIR_REVERSE` | 0 | 1 | 反转，OUT2 输出 PWM |
| `MOTOR_DIR_BRAKE` | 1 | 1 | 短接制动 |

### 4.3 `MotorStatus`

```c
typedef enum
{
    MOTOR_OK = 0,
    MOTOR_BUSY,
    MOTOR_TIMEOUT,
    MOTOR_ERR_PARAM,
    MOTOR_ERR_BUSY
} MotorStatus;
```

| 返回值 | 含义 | 典型场景 |
|---|---|---|
| `MOTOR_OK` | 成功/空闲/转动完成 | `Motor_Init()` 后、`Motor_Stop()` 后、正常转完 |
| `MOTOR_BUSY` | 正在执行转动 | `Motor_MoveTurns()` 成功后、`Motor_Update()` 未到目标时 |
| `MOTOR_TIMEOUT` | 超时，已自动停车 | 编码器方向反、轮子被卡、速度太低、超时太短 |
| `MOTOR_ERR_PARAM` | 参数错误或模块未初始化 | 指针为 NULL、速度 >100、方向不是正/反、圈数 ≤0 |
| `MOTOR_ERR_BUSY` | 上一次转动还没结束 | 在 `MOTOR_BUSY` 期间再次调用 `Motor_MoveTurns()` |

> 超时后 `Motor_Update()` / `Motor_GetStatus()` 会一直返回 `MOTOR_TIMEOUT`，直到下一次 `Motor_MoveTurns()`、`Motor_Stop()` 或 `Motor_ResetCount()`。

### 4.4 `Motor`

```c
typedef struct Motor Motor;
```

`Motor` 是不透明类型，内部字段只在 `motor.c` 中可见。应用层只持有 `Motor *`，不要自行访问结构体成员。

---

## 5. 常量

| 宏 | 默认值 | 说明 |
|---|---:|---|
| `MOTOR_COUNTS_PER_REV` | 1560 | 车轮转一圈的编码器计数 |
| `MOTOR_DEFAULT_TIMEOUT_MS` | 5000 | `Motor_Move()` / `Motor_Rotate()` 的默认超时 |

---

## 6. 接口说明

### 6.0 接口速查

| 接口 | 作用 | 是否阻塞 |
|---|---|---|
| `Motor_Init(id)` | 初始化并返回电机指针 | 否 |
| `Motor_SetDirection(motor, dir)` | 设置方向 | 否 |
| `Motor_SetSpeed(motor, percent)` | 设置占空比 | 否 |
| `Motor_SetDirSpeed(motor, dir, percent)` | 安全换向并设置占空比 | 否 |
| `Motor_Stop(motor)` | 停止并取消转动 | 否 |
| `Motor_Brake(motor)` | 短接制动 | 否 |
| `Motor_MoveTurns(...)` | 非阻塞转指定圈数 | 否 |
| `Motor_RotateTurns(...)` | 阻塞转指定圈数 | **是** |
| `Motor_Move(...)` | 非阻塞转圈，默认 5 s 超时 | 否 |
| `Motor_Rotate(...)` | 阻塞转圈，默认 5 s 超时 | **是** |
| `Motor_Update(motor)` | 读编码器并推进转动状态 | 否 |
| `Motor_IsBusy(motor)` | 查询是否正在转 | 否 |
| `Motor_GetStatus(motor)` | 查询最近一次转动状态 | 否 |
| `Motor_GetId(motor)` | 查询电机编号（左/右） | 否 |
| `Motor_GetCount(motor)` | 读软件累计计数 | 否 |
| `Motor_GetRawCounter(motor)` | 读硬件计数器原始值 | 否 |
| `Motor_GetTurns(motor)` | 读累计圈数 | 否 |
| `Motor_ResetCount(motor)` | 停车并清零计数 | 否 |

### 6.1 `Motor_Init`

```c
Motor *Motor_Init(MotorId id);
```

初始化指定电机并返回对象指针。

| 参数 | 类型 | 说明 |
|---|---|---|
| `id` | `MotorId` | `MOTOR_LEFT` 或 `MOTOR_RIGHT` |

| 返回值 | 说明 |
|---|---|
| `Motor *` | 成功，返回该电机的静态对象指针 |
| `NULL` | `id` 非法 |

执行内容：

1. 方向线设为 `MOTOR_DIR_STOP`；
2. PWM 占空比清零；
3. 启动对应 PWM 通道；
4. 硬件编码器计数器清零并启动；
5. 软件累计计数清零。

注意事项：

- 必须在 CubeMX 生成的 `MX_GPIO_Init()`、`MX_TIM1_Init()`、`MX_TIM2_Init()`、`MX_TIM4_Init()` 之后调用。
- 同一个 `id` 重复调用会直接返回同一个指针，**不会重复初始化，也不会清零计数**。
- 本工程中 `main.c` 在 CubeMX 外设初始化完成后直接调用 `Motor_Init(MOTOR_LEFT)` 和 `Motor_Init(MOTOR_RIGHT)`。

---

### 6.2 `Motor_SetDirection`

```c
void Motor_SetDirection(Motor *motor, MotorDir dir);
```

| 参数 | 说明 |
|---|---|
| `motor` | `Motor_Init()` 返回的指针；`NULL` 时函数直接返回 |
| `dir` | `MOTOR_DIR_STOP` / `FORWARD` / `REVERSE` / `BRAKE` |

返回值：无。

注意：运行中换向前应先把占空比降为 0，或直接使用 `Motor_SetDirSpeed()`。

---

### 6.3 `Motor_SetSpeed`

```c
void Motor_SetSpeed(Motor *motor, uint8_t percent);
```

| 参数 | 说明 |
|---|---|
| `motor` | 电机指针；`NULL` 时直接返回 |
| `percent` | 占空比 0～100；大于 100 按 100 处理 |

返回值：无。

说明：

- 只改 PWM 占空比，不改方向线。
- `percent=0` 时输出低电平，但不改变 IN1/IN2；要彻底停止请用 `Motor_Stop()`。
- 实际转速会随电池电压和负载变化，本接口不构成转速闭环。

---

### 6.4 `Motor_SetDirSpeed`

```c
void Motor_SetDirSpeed(Motor *motor, MotorDir dir, uint8_t percent);
```

安全换向并设置速度，执行顺序：

```text
占空比清零 → 设置方向 → 设置新占空比
```

| 参数 | 说明 |
|---|---|
| `motor` | 电机指针；`NULL` 时直接返回 |
| `dir` | 目标方向 |
| `percent` | 目标占空比 0～100 |

返回值：无。

适合用于“正转直接切反转”的场合，避免带载瞬间反接。

---

### 6.5 `Motor_Stop`

```c
void Motor_Stop(Motor *motor);
```

停止电机并取消尚未完成的转动。

行为：

- 占空比清零；
- 方向线设为 `MOTOR_DIR_STOP`（IN1=IN2=0，滑行）；
- `move_active=0`；
- 状态置为 `MOTOR_OK`。

| 参数 | 说明 |
|---|---|
| `motor` | 电机指针；`NULL` 时直接返回 |

返回值：无。

> 如果正在执行 `Motor_MoveTurns()`，调用本函数会取消该次转动，后续 `Motor_IsBusy()` 返回 0，`Motor_GetStatus()` 返回 `MOTOR_OK`。

---

### 6.6 `Motor_Brake`

```c
void Motor_Brake(Motor *motor);
```

短接制动，方向线设为 IN1=IN2=1，**保持当前 PWM 占空比不变**。

| 参数 | 说明 |
|---|---|
| `motor` | 电机指针；`NULL` 时直接返回 |

返回值：无。

说明：

- 会取消当前转动（`move_active=0`，状态 `MOTOR_OK`）。
- 需要完全停止输出时再调用 `Motor_Stop()`。

---

### 6.7 `Motor_MoveTurns`

```c
MotorStatus Motor_MoveTurns(Motor *motor, uint8_t speed_percent,
                            MotorDir dir, float turns, uint32_t timeout_ms);
```

非阻塞转动指定圈数：启动后立即返回，不等待转动完成。

| 参数 | 类型 | 说明 |
|---|---|---|
| `motor` | `Motor *` | 电机指针 |
| `speed_percent` | `uint8_t` | 占空比 0～100 |
| `dir` | `MotorDir` | 只允许 `MOTOR_DIR_FORWARD` 或 `MOTOR_DIR_REVERSE` |
| `turns` | `float` | 目标车轮圈数，必须 >0 且 ≤100000 |
| `timeout_ms` | `uint32_t` | 超时时间，单位 ms；0 表示不超时（不建议） |

| 返回值 | 说明 |
|---|---|
| `MOTOR_BUSY` | 启动成功，正在转动 |
| `MOTOR_ERR_PARAM` | 指针为空、速度 >100、方向不是正/反、圈数非法 |
| `MOTOR_ERR_BUSY` | 该电机上一次转动还没结束 |

关键行为：

- 目标计数 = `turns × 1560`，四舍五入到整数计数。
- 目标位置**相对于本次调用的当前位置**，不是绝对位置。
- 启动前会先调用一次 `Motor_Update()`，确保起始计数是最新的。
- 之后必须在主循环持续调用 `Motor_Update()`：
  - 到达目标：占空比清零、方向停止、状态 `MOTOR_OK`；
  - 超时：占空比清零、方向停止、状态 `MOTOR_TIMEOUT`。
- 到达目标后立即关输出，机械惯性可能造成少量过冲；要更准可降低占空比。

示例：

```c
Motor_MoveTurns(left, 50, MOTOR_DIR_FORWARD, 4.0f, 5000U);
while (Motor_IsBusy(left)) {
    Motor_Update(left);
    HAL_Delay(1);
}
```

---

### 6.8 `Motor_RotateTurns`

```c
MotorStatus Motor_RotateTurns(Motor *motor, uint8_t speed_percent,
                              MotorDir dir, float turns, uint32_t timeout_ms);
```

阻塞转动指定圈数：函数内部循环调用 `Motor_Update()`，直到转完或超时才返回。

参数与 `Motor_MoveTurns()` 完全相同。

| 返回值 | 说明 |
|---|---|
| `MOTOR_OK` | 已到达目标圈数并停车 |
| `MOTOR_TIMEOUT` | 超时，已自动停车 |
| `MOTOR_ERR_PARAM` | 参数错误 |
| `MOTOR_ERR_BUSY` | 上一次转动还没结束 |

注意：

- 阻塞期间不会更新其他电机；左右轮要同时转时请用 `Motor_MoveTurns()` + `Motor_Update()`。
- 内部使用 `HAL_Delay(1)`，需要 SysTick 正常工作，不能在中断里调用。
- `timeout_ms=0` 时会一直等待，可能卡死，不建议。

---

### 6.9 `Motor_Move` / `Motor_Rotate`

```c
MotorStatus Motor_Move(Motor *motor, uint8_t speed_percent,
                       MotorDir dir, float turns);

MotorStatus Motor_Rotate(Motor *motor, uint8_t speed_percent,
                         MotorDir dir, float turns);
```

使用默认超时 `MOTOR_DEFAULT_TIMEOUT_MS`（5000 ms）的简化版本：

- `Motor_Move()` = `Motor_MoveTurns(..., 5000U)`，非阻塞；
- `Motor_Rotate()` = `Motor_RotateTurns(..., 5000U)`，阻塞。

参数和返回值分别与对应的 `...Turns` 版本相同。

示例：

```c
Motor *left = Motor_Init(MOTOR_LEFT);

// 阻塞：50% 占空比正转 4 圈，默认 5 s 超时
MotorStatus st = Motor_Rotate(left, 50, MOTOR_DIR_FORWARD, 4.0f);
if (st != MOTOR_OK) {
    // MOTOR_TIMEOUT / MOTOR_ERR_PARAM / MOTOR_ERR_BUSY
}
```

---

### 6.10 `Motor_Update`

```c
MotorStatus Motor_Update(Motor *motor);
```

读取编码器、累计计数并推进转动状态。主循环里每 1～10 ms 调用一次。

| 参数 | 说明 |
|---|---|
| `motor` | 电机指针 |

| 返回值 | 说明 |
|---|---|
| `MOTOR_BUSY` | 正在转动，尚未到目标 |
| `MOTOR_OK` | 空闲，或本次转动正常完成 |
| `MOTOR_TIMEOUT` | 本次转动超时，已自动停车 |
| `MOTOR_ERR_PARAM` | 指针为空或模块未初始化 |

实现细节：

- 读取 16 位硬件计数器，用有符号 16 位差值累加到 `int32_t`，自动处理 `0xFFFF → 0x0000` 回绕。
- 正常调用频率下不会丢计数；如果超过约 6.9 s 才调用一次（366 RPM 时计数器转一圈），可能丢失计数。
- 空闲时返回的是上一次转动的最终状态；超时状态会保持到下一次转动或 `Motor_Stop()`。

---

### 6.11 `Motor_IsBusy`

```c
uint8_t Motor_IsBusy(const Motor *motor);
```

| 返回值 | 说明 |
|---|---|
| 1 | 正在执行 `Motor_MoveTurns()` / `Motor_RotateTurns()` |
| 0 | 空闲，或 `motor == NULL` |

---

### 6.12 `Motor_GetStatus`

```c
MotorStatus Motor_GetStatus(const Motor *motor);
```

返回最近一次转动的状态。`motor == NULL` 时返回 `MOTOR_ERR_PARAM`。

---

### 6.13 `Motor_GetCount`

```c
int32_t Motor_GetCount(const Motor *motor);
```

返回软件累计计数，带符号：

- 正转增加，反转减少；
- 单位是编码器计数，不是圈；
- 只有调用过 `Motor_Update()` 后才更新；
- `motor == NULL` 时返回 0。

换算：

```text
圈数      = count / 1560
角度(°)   = count × 360 / 1560
```

---

### 6.14 `Motor_GetRawCounter`

```c
uint16_t Motor_GetRawCounter(const Motor *motor);
```

直接读取硬件编码器计数器的当前值，范围 0～65535，主要用于调试。

- 不经过软件累计，会回绕；
- `motor == NULL` 或未初始化时返回 0。

---

### 6.15 `Motor_GetTurns`

```c
float Motor_GetTurns(const Motor *motor);
```

返回累计圈数：

```c
return (float)total_counts / 1560.0f;
```

- 带符号；
- `motor == NULL` 时返回 0.0f；
- 依赖 `Motor_Update()` 的累计结果。

---

### 6.16 `Motor_ResetCount`

```c
void Motor_ResetCount(Motor *motor);
```

清零计数，常用于手动计圈模式。

行为：

1. 调用 `Motor_Stop()`；
2. 软件累计计数清零；
3. 硬件编码器计数器清零；
4. 状态置为 `MOTOR_OK`。

| 参数 | 说明 |
|---|---|
| `motor` | 电机指针；`NULL` 时直接返回 |

返回值：无。

---

### 6.17 `Motor_GetId`

```c
MotorId Motor_GetId(const Motor *motor);
```

查询电机编号。

| 参数 | 说明 |
|---|---|
| `motor` | 电机指针 |

| 返回值 | 说明 |
|---|---|
| `MOTOR_LEFT` | 左电机 |
| `MOTOR_RIGHT` | 右电机 |
| `MOTOR_ID_COUNT` | `motor == NULL` |

用于需要根据电机对象判断左右通道的模块，例如 `EncoderUsart_Init()`。

---

## 7. 转动状态机

### 7.1 非阻塞流程

```text
Motor_MoveTurns()
        │
        ▼
     MOTOR_BUSY
        │
        │  主循环持续调用 Motor_Update()
        ▼
 ┌───────────────┐
 │ 到达目标计数？ │──是──► 停车，MOTOR_OK
 └───────────────┘
        │否
        ▼
 ┌───────────────┐
 │   超时？       │──是──► 停车，MOTOR_TIMEOUT
 └───────────────┘
        │否
        └──► 继续 MOTOR_BUSY
```

### 7.2 阻塞流程

```text
Motor_RotateTurns()
        │
        ▼
Motor_MoveTurns()
        │
        ▼
while (Motor_IsBusy()) {
    Motor_Update();
    HAL_Delay(1);
}
        │
        ▼
返回 MOTOR_OK / MOTOR_TIMEOUT
```

---

## 8. 使用示例

### 8.1 单电机阻塞转 4 圈

```c
#include "motor.h"

Motor *left = Motor_Init(MOTOR_LEFT);
if (left == NULL) {
    Error_Handler();
}

MotorStatus st = Motor_Rotate(left, 50, MOTOR_DIR_FORWARD, 4.0f);
if (st == MOTOR_OK) {
    // 正常转完
} else if (st == MOTOR_TIMEOUT) {
    // 检查编码器方向、接线、轮子是否被卡
}
```

### 8.2 左右轮同时正转 4 圈，停 0.5 s，再反转 4 圈

```c
Motor *left  = Motor_Init(MOTOR_LEFT);
Motor *right = Motor_Init(MOTOR_RIGHT);

// 正转
Motor_Move(left, 50, MOTOR_DIR_FORWARD, 4.0f);
Motor_Move(right, 50, MOTOR_DIR_FORWARD, 4.0f);

while (Motor_IsBusy(left) || Motor_IsBusy(right)) {
    Motor_Update(left);
    Motor_Update(right);
    HAL_Delay(1);
}

if (Motor_GetStatus(left) == MOTOR_TIMEOUT ||
    Motor_GetStatus(right) == MOTOR_TIMEOUT) {
    // 处理超时
}

HAL_Delay(500);

// 反转
Motor_Move(left, 50, MOTOR_DIR_REVERSE, 4.0f);
Motor_Move(right, 50, MOTOR_DIR_REVERSE, 4.0f);

while (Motor_IsBusy(left) || Motor_IsBusy(right)) {
    Motor_Update(left);
    Motor_Update(right);
    HAL_Delay(1);
}
```

### 8.3 持续转动，不关心圈数

```c
Motor_SetDirSpeed(left, MOTOR_DIR_FORWARD, 60);   // 左轮 60% 正转
Motor_SetDirSpeed(right, MOTOR_DIR_FORWARD, 60);  // 右轮 60% 正转

// ... 运行一段时间 ...

Motor_Stop(left);
Motor_Stop(right);
```

### 8.4 读取计数和圈数

```c
Motor_Update(left);

int32_t counts = Motor_GetCount(left);
float turns = Motor_GetTurns(left);
uint16_t raw = Motor_GetRawCounter(left);
```

### 8.5 自定义超时

```c
// 30% 占空比反转 2.5 圈，最多等 10 s
MotorStatus st = Motor_RotateTurns(left, 30, MOTOR_DIR_REVERSE, 2.5f, 10000U);
```

---

## 9. 初始化顺序

```text
HAL_Init()
    ↓
SystemClock_Config()
    ↓
MX_GPIO_Init()
    ↓
MX_TIM1_Init()       // PWM
MX_TIM2_Init()       // 右编码器
MX_TIM4_Init()       // 左编码器
    ↓
Motor_Init(MOTOR_LEFT)
Motor_Init(MOTOR_RIGHT)
```

`Motor_Init()` 内部会启动 PWM 和编码器，因此不能早于 CubeMX 的 `MX_*_Init()`。

---

## 10. 调用约束与注意事项

1. **不要在中断里调用阻塞接口**：`Motor_Rotate()` / `Motor_RotateTurns()` 内部有 `HAL_Delay(1)`。
2. **非阻塞转动必须持续调用 `Motor_Update()`**：建议 1～10 ms 一次；超过约 6.9 s 不调用可能因 16 位计数器回绕丢计数。
3. **不要混用底层接口和转动接口**：
   - `Motor_MoveTurns()` 运行期间再调用 `Motor_SetSpeed()` / `Motor_SetDirSpeed()`，转动状态机仍会继续，可能在你不想停的时候停车。
   - 需要中止转动请用 `Motor_Stop()`。
4. **换向前先降速**：直接 `Motor_SetDirection()` 前应先把占空比设为 0，或使用 `Motor_SetDirSpeed()`。
5. **圈数是相对本次调用起点**：连续两次 `MoveTurns(..., 4.0f)` 会转 8 圈，不是回到绝对 4 圈位置。
6. **超时时间要留够**：低速、负载重、圈数多时适当增大 `timeout_ms`。
7. **编码器方向必须匹配**：`MOTOR_*_ENC_INVERTED` 配错会表现为“一直转但永远不到目标”，最后超时。
8. **到达目标后滑行**：`Motor_Update()` 到点后立即关输出，机械惯性可能让实际位置略超目标计数。
9. **模块非线程安全**：所有接口默认只在主循环上下文调用，不要和中断同时操作同一个 `Motor`。
10. **`Motor_GetCount()` 不读硬件**：它返回软件累计值，读之前应先调用 `Motor_Update()`。

---

## 11. 当前工程中的使用方式

旧的 `motor_test.c/h`、`encoder_test.c/h`、`app_config.h` 已删除，工程现在只保留 `motor.c/h` 和 `encoder_usart.c/h`。`main.c` 当前逻辑：

```c
Motor *left_motor  = Motor_Init(MOTOR_LEFT);
Motor *right_motor = Motor_Init(MOTOR_RIGHT);

EncoderUsart_Init(left_motor);
EncoderUsart_Init(right_motor);

while (1)
{
    Motor_Update(left_motor);
    Motor_Update(right_motor);
    EncoderUsart_Task();
    HAL_Delay(1);
}
```

当前 `main.c` 处于**纯编码器读取状态**：不驱动电机，主循环持续调用 `Motor_Update()` 和 `EncoderUsart_Task()`；手动转动车轮即可在串口观察计数、圈数和角度。

其他运动示例：

```c
/* 单电机阻塞转 4 圈；阻塞期间不会更新串口 */
Motor_Rotate(left_motor, 50, MOTOR_DIR_FORWARD, 4.0f);

/* 左右轮同时转，非阻塞 */
Motor_Move(left_motor, 50, MOTOR_DIR_FORWARD, 4.0f);
Motor_Move(right_motor, 50, MOTOR_DIR_FORWARD, 4.0f);
```

### 11.1 编码器串口输出

`Core/Src/encoder_usart.c` 每 100 ms 读取一次 `Motor_GetCount()`，输出到：

- USART1：PA9 = TX，PA10 = RX，115200 8N1（主串口）
- USART3：PB10 = TX，PB11 = RX，115200 8N1（镜像）

输出格式：

```text
T=...ms | L d=+... tot=+... rev=+x.xx ang=+xxxx.xx rpm=+xx.x | R ...
```

| 字段 | 含义 |
|---|---|
| `T` | 上电后的毫秒数 |
| `d` | 本次 100 ms 采样内的计数增量 |
| `tot` | 累计计数 |
| `rev` | 累计圈数，两位小数 |
| `ang` | 累计角度，单位 0.01° |
| `rpm` | 瞬时转速，单位 RPM，一位小数 |

默认只在左右轮计数发生变化时发送；需要每 100 ms 无条件输出，把 `encoder_usart.h` 里的 `ENCODER_USART_ALWAYS_PRINT` 改为 `1`。

> `EncoderUsart_Task()` 只读取软件累计值，因此主循环必须先调用 `Motor_Update()`。

---

## 12. 常见问题

| 现象 | 可能原因 | 处理 |
|---|---|---|
| `Motor_Init()` 返回 `NULL` | `id` 不是 `MOTOR_LEFT` / `MOTOR_RIGHT` | 检查传入的枚举 |
| 电机完全不转 | STBY 不为高、VM 没电、方向/速度没设置 | 检查 TB6612 供电和 `Motor_SetDirSpeed()` |
| `MoveTurns` 返回 `MOTOR_ERR_BUSY` | 上一次转动还没结束 | 等待 `Motor_IsBusy()` 变 0，或先 `Motor_Stop()` |
| 圈数永远转不到，最后超时 | 编码器方向反了、编码器没计数、轮子被卡、速度太低 | 检查 `MOTOR_*_ENC_INVERTED`、编码器接线、超时时间 |
| 计数一直是 0 | 编码器 3.3 V 供电、GND、A/B 接线、TIM 未启动 | 用 `Motor_GetRawCounter()` 或示波器检查 A/B 和计数 |
| 手转正转计数为负 | 编码器 A/B 接反或计数方向相反 | 把对应 `MOTOR_*_ENC_INVERTED` 改为 1 |
| 实际转过的圈数偏大 | 到达目标后滑行过冲 | 降低占空比，或接受机械惯性误差 |
| 转向和标注相反 | 电机线序或方向线定义相反 | 把对应 `MOTOR_*_DIR_INVERTED` 改为 1 |
| 计数大约翻倍/减半 | 编码器模式不是四倍频，或每圈计数不是 1560 | 检查 CubeMX TIM2/TIM4 编码器模式和 13×30 参数 |
| 编译提示 `Motor` 未定义 | 没有包含 `motor.h` | 在源文件加入 `#include "motor.h"` |

---

## 13. 文件对应关系

| 内容 | 文件 |
|---|---|
| 电机对象、编码器累计、转圈 API | `Core/Inc/motor.h`、`Core/Src/motor.c` |
| 编码器串口输出 | `Core/Inc/encoder_usart.h`、`Core/Src/encoder_usart.c` |
| 应用入口和主循环 | `Core/Src/main.c` |
| CubeMX 引脚和外设配置 | `car.ioc` |
| 本接口文档 | `doc/motor_api.md` |
| 编码器串口接口文档 | `doc/encoder_usart_api.md` |
