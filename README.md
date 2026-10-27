# STM32F103C8T6 + TB6612 电机控制工程

> 当前工程只保留电机模块 `Core/Src/motor.c` / `Core/Inc/motor.h`。  
> 完整的参数、返回值、状态机和示例见 `doc/motor_api.md`。

---

## 1. 引脚配置

| MCU 引脚 | 功能 | CubeMX 配置 | 标签 | 归属 |
|---|---|---|---|---|
| PA8 | TIM1_CH1 | AF Push-Pull，High speed | `PWMA` | 左轮 PWM（A 通道） |
| PA11 | TIM1_CH4 | AF Push-Pull，High speed | `PWMB` | 右轮 PWM（B 通道） |
| PB14 | GPIO 输出 | Push-Pull，初始低 | `BIN1` | 左轮方向 1（A 通道） |
| PB15 | GPIO 输出 | Push-Pull，初始低 | `BIN2` | 左轮方向 2（A 通道） |
| PB12 | GPIO 输出 | Push-Pull，初始低 | `AIN1` | 右轮方向 1（B 通道） |
| PB13 | GPIO 输出 | Push-Pull，初始低 | `AIN2` | 右轮方向 2（B 通道） |
| PB6 / PB7 | TIM4_CH1 / CH2 | Encoder Mode TI1 and TI2 | `ENC_LA` / `ENC_LB` | 左轮编码器 |
| PA15 / PB3 | TIM2_CH1 / CH2 | Encoder Mode TI1 and TI2（TIM2 部分重映射 1） | `ENC_RB` / `ENC_RA` | 右轮编码器 |
| PA5 | GPIO 输入 | Input，Pull-up | `KEY1` | 用户按键，按下为低 |
| PA9 / PA10 | USART1_TX / RX | Asynchronous 115200 | `UART1_TX` / `UART1_RX` | 主串口 |
| PB10 / PB11 | USART3_TX / RX | Asynchronous 115200 | `BT_TX` / `BT_RX` | 镜像/蓝牙 |
| PA13 / PA14 | SWD | Serial Wire | — | 下载调试 |

电机归属：

| 电机 | PWM | 方向 | 编码器 |
|---|---|---|---|
| 左轮（A 通道） | PA8 / TIM1_CH1 | PB14 / PB15 | PB6 / PB7 / TIM4 |
| 右轮（B 通道） | PA11 / TIM1_CH4 | PB12 / PB13 | PA15 / PB3 / TIM2（重映射） |

> CubeMX 标签 `AIN1/AIN2` 实际接在 PB12/PB13（B 通道），`BIN1/BIN2` 实际接在 PB14/PB15（A 通道），标签名和 A/B 通道名是反的；接线以引脚号为准。完整说明见 `doc/pin_config.md`。

TIM1：PSC=0，ARR=399，8 MHz 时钟下 PWM 频率 **20 kHz**。

---

## 2. 电机模块

所有电机和编码器操作都在 `motor.c` / `motor.h`：

```c
#include "motor.h"

Motor *left  = Motor_Init(MOTOR_LEFT);
Motor *right = Motor_Init(MOTOR_RIGHT);
```

### 2.1 持续转动

```c
Motor_SetDirSpeed(left, MOTOR_DIR_FORWARD, 60);   // 左轮 60% 正转
Motor_SetDirSpeed(right, MOTOR_DIR_FORWARD, 60);  // 右轮 60% 正转

Motor_Stop(left);
Motor_Stop(right);
```

### 2.2 转动指定圈数

圈数指**车轮/输出轴圈数**，1560 计数 = 1 圈。

```c
// 阻塞版：函数返回时已经转完
MotorStatus st = Motor_Rotate(left, 50, MOTOR_DIR_FORWARD, 4.0f);

// 非阻塞版：立即返回，主循环持续调用 Motor_Update()
Motor_Move(left, 50, MOTOR_DIR_FORWARD, 4.0f);
while (Motor_IsBusy(left)) {
    Motor_Update(left);
    HAL_Delay(1);
}
```

默认超时 5 s；需要自定义超时用 `Motor_RotateTurns()` / `Motor_MoveTurns()`。

### 2.3 读取编码器

```c
Motor_Update(left);

int32_t counts = Motor_GetCount(left);   // 累计计数
float turns    = Motor_GetTurns(left);   // 累计圈数
```

`Motor_Update()` 建议每 1～10 ms 调用一次，同时负责累计计数、判断是否到达目标圈数和超时。

### 2.4 串口输出

`encoder_usart.c/h` 负责把编码器数据发到串口：

```c
EncoderUsart_Init(left);          // 左右各初始化一次
EncoderUsart_Init(right);

while (1) {
    Motor_Update(left);
    Motor_Update(right);
    EncoderUsart_Task();          // 每 100 ms 采样一次
    HAL_Delay(1);
}
```

输出到 USART1（PA9，115200 8N1），同时镜像到 USART3（PB10）。默认只在计数变化时发送，格式：

```text
T=...ms | L d=+... tot=+... rev=+x.xx ang=+xxxx.xx rpm=+xx.x | R ...
```

需要每 100 ms 无条件输出，把 `encoder_usart.h` 里的 `ENCODER_USART_ALWAYS_PRINT` 改为 `1`。

### 2.5 方向修正

如果电机转向或编码器计数方向与标注相反，在 `motor.h` 修改：

```c
#define MOTOR_LEFT_DIR_INVERTED     0
#define MOTOR_RIGHT_DIR_INVERTED    0
#define MOTOR_LEFT_ENC_INVERTED     0
#define MOTOR_RIGHT_ENC_INVERTED    0
```

---

## 3. 当前 main.c

当前 `main.c` 是**左右电机一直转 + PA5 按键调速**版本：

1. 完成 CubeMX 生成的外设初始化；
2. `Motor_Init()` 取得左右电机指针，并启动编码器串口回传；
3. **左右两轮**都以 **20%** 占空比持续正转，**没有圈数限制**，直到复位/断电；
4. 主循环检测 **PA5**（输入上拉、按下为低）：每按一次左右**同时**跳档，20 → 40 → 60 → 80 → 100 → 回到 20；按住不放只算一次，松手再按才算下一次；
5. 每次换档串口补一行 `KEY: duty -> xx%`（USART1 + USART3 镜像），用来确认按键被识别；
6. 同时持续调用 `Motor_Update()` 和 `EncoderUsart_Task()`。

档位和消抖参数在 `main.c` 的 `USER CODE PD` 区：`DUTY_STEP_PERCENT`、`DUTY_MIN_PERCENT`、`DUTY_MAX_PERCENT`、`KEY_DEBOUNCE_MS`。

> 右通道方向线是物理反接的，`motor.h` 里 `MOTOR_RIGHT_DIR_INVERTED = 1`，所以代码里两边都写 `MOTOR_DIR_FORWARD` 时两轮同向。

---

## 4. 编译

```bash
cmake -S . -B build/homework -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake \
  -DCMAKE_BUILD_TYPE=Debug
cmake --build build/homework
```

输出：`build/homework/car.elf`。  
`build/` 已加入 `.gitignore`，不会提交编译产物。

---

## 5. 上板注意

- 编码器必须供 **3.3 V**；PA15 / PB3 虽 5 V 耐受也按 3.3 V 用。
- PA15 / PB3 默认是 JTAG 脚，`HAL_MspInit()` 里的 `__HAL_AFIO_REMAP_SWJ_NOJTAG()` 不能删；下载只能用 SWD。
- TB6612 的 `STBY` 必须为高，`VM` 接电机电源，所有地必须共地。
- 第一次测试先抬起车轮。
- 正反转或编码器计数方向不对时，优先改 `motor.h` 里的四个反相宏，不要改接线。

---

## 6. 文档

| 文档 | 内容 |
|---|---|
| `doc/motor_api.md` | **当前有效**：电机模块全部接口、参数、返回值、示例 |
| `doc/encoder_usart_api.md` | **当前有效**：编码器串口输出接口、格式、示例 |
| `doc/pin_config.md` | **当前有效**：修正后的电机/编码器引脚与通道配对 |
| `doc/HANDOVER.md` | **交接文档**：现状、遗留问题、验证清单、下一步 |
| `引脚配置.md` | 早期引脚记录，左右命名和编码器配对以 `doc/pin_config.md` 为准 |
| `TEST_GUIDE.md` | 之前作业二的记录，对应测试代码已删除，仅供参考 |
| `HW3_GUIDE.md` | 之前作业三的记录，对应测试代码已删除，仅供参考 |
