# 电机与编码器引脚配置（修正版）

> 本文件是当前工程**实际通道配对**的最终依据。  
> 依据：参考固件 `board_config.h` 的 A/B 通道定义，以及其串口自检结果：
>
> ```text
> #SELF,chA,L=812,R=0     → A 通道对应 TIM4 编码器（左轮）
> #SELF,chB,L=0,R=795     → B 通道对应 TIM2 编码器（右轮）
> ```

---

## 1. 修正结论

物理通道与左右轮的对应关系：

| 物理通道 | 左/右 | PWM | 方向线 | 编码器 |
|---|---|---|---|---|
| **A 通道** | **左轮** | **PA8 / TIM1_CH1** | **PB14 / PB15** | **PB6 / PB7 / TIM4** |
| **B 通道** | **右轮** | **PA11 / TIM1_CH4** | **PB12 / PB13** | **PA15 / PB3 / TIM2（重映射）** |

当前 `motor.c` 中的对象：

```c
MOTOR_LEFT  = A 通道 = PA8  + PB14/PB15 + TIM4
MOTOR_RIGHT = B 通道 = PA11 + PB12/PB13 + TIM2 (PA15/PB3)
```

之前的错误是：`MOTOR_LEFT` 用了 PA11 的 PWM，`MOTOR_RIGHT` 用了 PA8 的 PWM，导致**单电机转动时 PWM 和方向线分属不同 TB6612 通道**，两个通道都无法输出。

---

## 2. 完整引脚表

### 2.1 电机与编码器

| 功能 | MCU 引脚 | 定时器/资源 | CubeMX 标签 | 说明 |
|---|---|---|---|---|
| 左轮 PWM | **PA8** | TIM1_CH1 | `PWMA` | 20 kHz |
| 左轮 IN1 | **PB14** | GPIO 输出 | `BIN1` | 标签名见下方说明 |
| 左轮 IN2 | **PB15** | GPIO 输出 | `BIN2` | |
| 左轮编码器 A | **PB6** | TIM4_CH1 | `ENC_LA` | 3.3 V 供电 |
| 左轮编码器 B | **PB7** | TIM4_CH2 | `ENC_LB` | 3.3 V 供电 |
| 右轮 PWM | **PA11** | TIM1_CH4 | `PWMB` | 20 kHz |
| 右轮 IN1 | **PB12** | GPIO 输出 | `AIN1` | 标签名见下方说明 |
| 右轮 IN2 | **PB13** | GPIO 输出 | `AIN2` | |
| 右轮编码器 A | **PB3** | TIM2_CH2（重映射） | `ENC_RA` | 5 V 耐受，仍按 3.3 V 用 |
| 右轮编码器 B | **PA15** | TIM2_CH1（重映射） | `ENC_RB` | 5 V 耐受，仍按 3.3 V 用 |

### 2.1.1 TIM2 重映射（右编码器：PA0/PA1 → PA15/PB3）

右轮编码器已从 PA0/PA1 挪到 **PA15 / PB3**，用 TIM2 的**部分重映射 1**（Partial Remap 1）：

| 通道 | 引脚 | 说明 |
|---|---|---|
| TIM2_CH1 / ETR | **PA15** | 默认是 JTDI |
| TIM2_CH2 | **PB3** | 默认是 JTDO |
| TIM2_CH3 | PA2 | 名义通道，程序不启用；PA2 仍可当普通 GPIO / USART2 用 |
| TIM2_CH4 | PA3 | 同上 |

- 由 `HAL_TIM_Encoder_MspInit()` 里的 `__HAL_AFIO_REMAP_TIM2_PARTIAL_1()` 生效。
- **前提**：`HAL_MspInit()` 里的 `__HAL_AFIO_REMAP_SWJ_NOJTAG()` 必须在（CubeMX 里 SYS → Debug = **Serial Wire** 就会生成）。没有它 PA15/PB3 还是 JTAG 脚，编码器读不到数；SWD（PA13/PA14）下载调试不受影响。
- 为什么不用全重映射：全重映射把 TIM2_CH3/CH4 放到 **PB10/PB11**，和 USART3 抢脚；部分重映射 1 放到 PA2/PA3，没有冲突。
- **PB13 为什么不给编码器**：STM32F103 的 PB13 没有任何定时器输入通道（复用只有 TIM1_CH1N 互补输出、SPI2_SCK、I2C2_SCL、USART3_CTS），做不了硬件四倍频；而且它还要做右电机 `AIN2`。
- 连带变化：**PA0 / PA1 现在空闲**，可以留给 ADC 等外设（5 V 信号仍然不能进）。

### 2.2 串口与调试

| 功能 | MCU 引脚 | 资源 | 说明 |
|---|---|---|---|
| 串口 1 TX | PA9 | USART1_TX | 主串口，115200 8N1 |
| 串口 1 RX | PA10 | USART1_RX | 接收命令/预留 |
| 串口 3 TX | PB10 | USART3_TX | 镜像/蓝牙 |
| 串口 3 RX | PB11 | USART3_RX | 镜像/蓝牙 |
| SWDIO | PA13 | SWD | 下载调试 |
| SWCLK | PA14 | SWD | 下载调试 |

---

## 3. 代码中的对应关系

`Core/Src/motor.c`：

```c
/* 左轮：A 通道 */
.id = MOTOR_LEFT,
.pwm_tim = &htim1,
.pwm_channel = TIM_CHANNEL_1,       // PA8
.dir_port = BIN1_GPIO_Port,
.in1_pin = BIN1_Pin,                // PB14
.in2_pin = BIN2_Pin,                // PB15
.enc_tim = &htim4,                  // PB6/PB7

/* 右轮：B 通道 */
.id = MOTOR_RIGHT,
.pwm_tim = &htim1,
.pwm_channel = TIM_CHANNEL_4,       // PA11
.dir_port = AIN1_GPIO_Port,
.in1_pin = AIN1_Pin,                // PB12
.in2_pin = AIN2_Pin,                // PB13
.enc_tim = &htim2,                  // PA15/PB3（TIM2 部分重映射 1）
```

---

## 4. CubeMX 标签命名说明

当前 `main.h` / `car.ioc` 里的标签名和物理 A/B 通道是**反的**：

| 物理通道 | 实际方向引脚 | 当前 CubeMX 标签 |
|---|---|---|
| A 通道（左轮） | PB14 / PB15 | `BIN1` / `BIN2` |
| B 通道（右轮） | PB12 / PB13 | `AIN1` / `AIN2` |

这只是**名字叫反了**，引脚号和 `motor.c` 的配对是正确的：

- 左轮用 `BIN1/BIN2`，实际是 PB14/PB15；
- 右轮用 `AIN1/AIN2`，实际是 PB12/PB13。

**接线时以引脚号为准，不要只看 AIN/BIN 名字。**

如果以后想消除这个命名混乱，可以在 CubeMX 里把：
- PB14/PB15 改名为 `AIN1/AIN2`；
- PB12/PB13 改名为 `BIN1/BIN2`；

重新生成后，`motor.c` 里左右轮使用的标签也要跟着互换。

---

## 5. 方向与编码器修正

如果接线正确但方向或计数符号不对，改 `Core/Inc/motor.h`：

```c
#define MOTOR_LEFT_DIR_INVERTED     0
#define MOTOR_RIGHT_DIR_INVERTED    0
#define MOTOR_LEFT_ENC_INVERTED     0
#define MOTOR_RIGHT_ENC_INVERTED    0
```

| 现象 | 修改 |
|---|---|
| 左轮 `MOTOR_DIR_FORWARD` 实际反转 | `MOTOR_LEFT_DIR_INVERTED = 1` |
| 右轮 `MOTOR_DIR_FORWARD` 实际反转 | `MOTOR_RIGHT_DIR_INVERTED = 1` |
| 左轮前进时计数为负 | `MOTOR_LEFT_ENC_INVERTED = 1` |
| 右轮前进时计数为负 | `MOTOR_RIGHT_ENC_INVERTED = 1` |

> AB 两相接反只会让计数**稳定变号**；如果串口出现 `d=+1、-1、+1、-1` 且 `tot` 在 0 附近来回跳，通常是**只有一相在动、另一相缺失/悬空/干扰**，不是简单的 AB 反相。

---

## 6. 电气约束

- 编码器一律供 **3.3 V**；PA15 / PB3 虽然 5 V 耐受，逻辑高电平也请按 3.3 V 使用。
- PB6 / PB7 虽可耐 5 V，也建议按 3.3 V 使用。
- 编码器若是开漏输出，A/B 到 3.3 V 各加 **4.7 kΩ～10 kΩ** 外部上拉；MCU 内部上拉约 40 kΩ，抗干扰较弱。
- TB6612 的 `STBY` 必须为高，`VM` 接电机电源，MCU/TB6612/电机电源必须共地。
- PWM 频率：8 MHz / (399 + 1) = **20 kHz**。

---

## 7. 上板验证

车轮架空，烧录当前固件后观察串口：

1. **左轮正转 4 圈**
   - `L tot` 应从 0 增加到约 **+6240**；
   - `L rev` 应接近 `+4.00`；
   - `L rpm` 应为正值。

2. **左轮反转 4 圈**
   - `L tot` 应回到约 **0**；
   - `L rev` 应回到约 `0.00`；
   - `L rpm` 应为负值。

3. 如果结果不对：

| 现象 | 检查 |
|---|---|
| 电机完全不转 | PWM 通道是否 PA8/CH1、PA11/CH4；STBY/VM；共地 |
| 电机转但 `tot` 不变 | 编码器 3.3 V、A/B 接线、PB6/PB7、PA15/PB3 |
| 右轮 `tot` 恒 0，PA15/PB3 上确实有脉冲 | 重映射没生效：检查 `__HAL_AFIO_REMAP_TIM2_PARTIAL_1()` 和 `__HAL_AFIO_REMAP_SWJ_NOJTAG()` |
| `tot` 稳定为负 | 改对应的 `MOTOR_*_ENC_INVERTED` |
| `tot` 在 0 附近 ±1 跳 | 一相缺失/悬空/干扰，查 A/B 线和外部上拉 |
| 转的方向和预期相反 | 改对应的 `MOTOR_*_DIR_INVERTED` |
| 只转 3.6 圈左右就停 | 正常机械误差；降低速度或参考参考固件的最小占空比/刹车策略 |

---

## 8. 相关文件

| 内容 | 文件 |
|---|---|
| 电机对象和通道映射 | `Core/Src/motor.c`、`Core/Inc/motor.h` |
| CubeMX 引脚配置 | `car.ioc`、`Core/Inc/main.h` |
| 编码器串口输出 | `Core/Src/encoder_usart.c` |
| 主流程 | `Core/Src/main.c` |
| 参考固件通道定义 | `Downloads/firmware/firmware/include/board_config.h` |
