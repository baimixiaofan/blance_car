# 编码器串口模块接口文档

> 模块文件：`Core/Inc/encoder_usart.h`、`Core/Src/encoder_usart.c`  
> 依赖模块：`Core/Inc/motor.h`、`Core/Src/motor.c`  
> 适用工程：STM32F103C8T6 + TB6612FNG + AB 增量式编码器  
> 相关文档：`doc/motor_api.md`

---

## 1. 模块职责

`encoder_usart.c` 负责把 `Motor` 对象里的编码器数据格式化后发送到串口，不负责电机控制和编码器计数。

数据流：

```text
编码器硬件
    ↓
Motor_Update()           // 读取 TIM2/TIM4，累计计数
    ↓
Motor_GetCount()
    ↓
EncoderUsart_Task()      // 每 100 ms 取一次数据
    ↓
USART1 / USART3
```

主要功能：

- 每 100 ms 采样左右轮累计计数；
- 计算 100 ms 内的计数增量；
- 计算累计圈数、累计角度和瞬时 RPM；
- 输出到 USART1，并镜像到 USART3；
- 默认只在计数变化时发送，避免停转后刷屏。

---

## 2. 串口硬件

| 串口 | 引脚 | 用途 | 参数 |
|---|---|---|---|
| USART1 | PA9 = TX，PA10 = RX | 主串口，接 USB-TTL | 115200，8 数据位，1 停止位，无校验 |
| USART3 | PB10 = TX，PB11 = RX | 镜像/蓝牙 | 115200，8 数据位，1 停止位，无校验 |

接线注意：

- USB-TTL 的 **RXD 接 PA9**，GND 必须和板子共地。
- USB-TTL 的 TXD 接 PA10 只在本模块需要接收命令时才有用；当前模块只发送，不接收。
- 两个串口输出完全相同的内容，接其中一个即可。
- PA9 / PA10 / PB10 / PB11 均可耐 5 V，但 USB-TTL 建议使用 3.3 V 电平版本，不要向板子反灌 5 V 电源。

---

## 3. 配置宏

定义在 `encoder_usart.h`：

| 宏 | 默认值 | 说明 |
|---|---:|---|
| `ENCODER_USART_PERIOD_MS` | 100 | 采样和发送周期，单位 ms |
| `ENCODER_USART_ALWAYS_PRINT` | 0 | 0=只在计数变化时发送；1=每 100 ms 无条件发送 |

修改这两个宏后需要重新编译并烧录，它们是编译期宏，不是运行时变量。

---

## 4. 数据类型

本模块不定义新的结构体或枚举，直接使用 `motor.h` 中的：

```c
typedef struct Motor Motor;
```

以及：

```c
int32_t Motor_GetCount(const Motor *motor);
```

---

## 5. 接口速查

| 接口 | 作用 | 返回值 |
|---|---|---|
| `EncoderUsart_Init(motor)` | 初始化一个电机的串口输出，左右各调用一次 | 无 |
| `EncoderUsart_Task()` | 周期采样并发送编码器数据 | 无 |

---

## 6. 接口说明

### 6.1 `EncoderUsart_Init`

```c
void EncoderUsart_Init(Motor *motor);
```

初始化一个电机的串口输出。需要分别传左、右电机指针，**调用两次**。

| 参数 | 类型 | 说明 |
|---|---|---|
| `motor` | `Motor *` | 左电机或右电机指针，通常为 `Motor_Init()` 的返回值 |

返回值：无。

内部通过 `Motor_GetId(motor)` 判断是左电机还是右电机：

- `MOTOR_LEFT`：保存为左通道，打印左轮信息；
- `MOTOR_RIGHT`：保存为右通道，打印右轮信息；
- `NULL` 或非法指针：直接返回，不做任何事。

执行内容：

1. 判断电机编号；
2. 保存对应电机指针；
3. 记录当前计数作为上一次采样值；
4. 第一次调用时打印模块标题和串口参数；
5. 每次调用打印对应通道的启动信息。

两次调用的典型输出：

```text
=== Encoder USART ===
USART1 PA9/PA10 + USART3 PB10/PB11, 115200 8N1
LEFT  ready: TIM4 (PB6/PB7), 1560 counts/rev
RIGHT ready: TIM2 (PA0/PA1), 1560 counts/rev
```

其中标题和串口参数只在第一次调用时打印。

调用顺序：

```text
MX_USART1_UART_Init()
MX_USART3_UART_Init()
        ↓
Motor_Init(MOTOR_LEFT)
Motor_Init(MOTOR_RIGHT)
        ↓
EncoderUsart_Init(left_motor)
EncoderUsart_Init(right_motor)
```

注意：

- 必须在 USART 初始化之后调用，否则启动信息发不出去。
- 必须在对应的 `Motor_Init()` 之后调用。
- **左右各调用一次**；只初始化一路时，`EncoderUsart_Task()` 不会输出（它需要左右两路数据才能组成完整一行）。
- 重复初始化同一路不会报错，但会重新记录该路的当前计数。

---

### 6.2 `EncoderUsart_Task`

```c
void EncoderUsart_Task(void);
```

周期调用，按 `ENCODER_USART_PERIOD_MS` 限速采样并发送。

| 参数 | 说明 |
|---|---|
| 无 | — |

返回值：无。

执行流程：

```text
距上次采样不足 100 ms？ ──是──► 直接返回
        │否
        ▼
读取 Motor_GetCount(left/right)
        ▼
计算本次增量 d
        ▼
计数与上次发送值相同？ ──是──► 直接返回（默认模式）
        │否
        ▼
计算 rev / ang / rpm
        ▼
USART1 发送
USART3 发送
```

注意：

- **必须先调用 `Motor_Update()`**。`Motor_GetCount()` 返回的是软件累计值，`EncoderUsart_Task()` 不会主动读编码器。
- 函数内部使用轮询 `HAL_UART_Transmit()`，正常一帧约 26 ms（两个串口各一行），会短暂阻塞主循环。
- 建议主循环保持 1 ms 调用 `Motor_Update()`，串口发送造成的短暂延迟不会丢失编码器计数。
- 不能在中断里调用。
- 模块非线程安全，只在主循环上下文使用。
- 左右两路都完成 `EncoderUsart_Init()` 后才会输出完整数据行。

---

## 7. 输出格式

每行以 `\r\n` 结尾：

```text
T=1234ms | L d=+156 tot=+6240 rev=+4.00 ang=+1440.00 rpm=+180.0 | R d=+150 tot=+6120 rev=+3.92 ang=+1412.31 rpm=+173.1
```

字段说明：

| 字段 | 单位 | 说明 |
|---|---|---|
| `T` | ms | 上电后的毫秒数（`HAL_GetTick()`） |
| `L` / `R` | — | 左轮 / 右轮 |
| `d` | 计数 | 本次采样周期内的计数增量（周期默认 100 ms），带符号 |
| `tot` | 计数 | 累计计数，带符号 |
| `rev` | 圈 | 累计圈数，两位小数 |
| `ang` | 0.01° | 累计角度绝对值，符号单独显示 |
| `rpm` | RPM | 瞬时转速，一位小数 |

示例解读：

```text
L d=+156 tot=+6240 rev=+4.00 ang=+1440.00 rpm=+180.0
```

表示左轮：

- 本次采样周期增加 156 个计数；
- 累计 6240 个计数；
- 累计 4.00 圈；
- 累计角度 1440.00°；
- 当前约 180.0 RPM。

---

## 8. 计算公式

```text
d      = tot_now - tot_previous

rev    = tot / 1560
ang    = |tot| × 360 / 1560
rpm    = d × 60000 / (1560 × elapsed_ms)
```

其中：

- `1560` = 13 线 × 30:1 减速比 × 4 倍频；
- `elapsed_ms` = 本次采样与上次采样的时间差，通常为 100 ms；
- 圈数和角度是**累计值**，不是本次增量；
- RPM 是本次采样窗口内的平均转速。

---

## 9. 发送策略

### 9.1 默认模式（`ENCODER_USART_ALWAYS_PRINT = 0`）

- 左右轮计数都没有变化：不发送；
- 任意一轮计数变化：发送一行完整数据；
- 车轮停稳后的第一次采样：补发最后一行，之后停止发送。

适合作业记录，不会在静止时刷屏。

### 9.2 连续模式（`ENCODER_USART_ALWAYS_PRINT = 1`）

- 每 `ENCODER_USART_PERIOD_MS` 无条件发送一行；
- 即使车轮静止也持续输出；
- 适合调试串口、观察 RPM 是否归零。

修改后需要重新编译。

---

## 10. 使用示例

### 10.1 当前工程中的用法

```c
#include "motor.h"
#include "encoder_usart.h"

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

当前 `main.c` 在左轮正转 4 圈、停 0.5 s、反转 4 圈的过程中调用 `EncoderUsart_Init()` 和 `EncoderUsart_Task()`，串口会持续输出编码器数据（默认每 100 ms 采样，计数变化时发送）。

### 10.2 手动转轮观察计数

不需要启动电机，直接用手转动车轮：

1. 烧录程序；
2. USB-TTL 接 PA9 / GND，打开串口助手；
3. 波特率 115200，8N1；
4. 手转左轮或右轮，串口会输出对应的 `d / tot / rev / ang / rpm`。

### 10.3 让电机转 4 圈并观察

要边转边看串口数据，必须用非阻塞的 `Motor_Move()`：

```c
Motor_Move(left_motor, 50, MOTOR_DIR_FORWARD, 4.0f);

while (Motor_IsBusy(left_motor)) {
    Motor_Update(left_motor);
    Motor_Update(right_motor);
    EncoderUsart_Task();
    HAL_Delay(1);
}
```

串口最终应显示左轮 `tot` 接近 `+6240`、`rev` 接近 `+4.00`（存在少量机械过冲属正常）。

> 不要在本模块的等待过程中使用阻塞的 `Motor_Rotate()`：它会卡住主循环，转动期间 `EncoderUsart_Task()` 不会执行，串口只会在转完后才更新。

---

## 11. 与其他模块的关系

| 模块 | 关系 |
|---|---|
| `motor.c/h` | 提供电机指针、编码器累计计数和 `Motor_Update()` |
| `encoder_usart.c/h` | 只读取 `Motor_GetCount()`，不修改电机状态 |
| `main.c` | 负责初始化顺序和主循环调度 |
| `car.ioc` | 配置 USART1 / USART3 和编码器定时器 |

`EncoderUsart_Task()` 不调用 `Motor_Update()`，也不控制电机；它只做“读数据 + 发串口”。

---

## 12. 调用约束与注意事项

1. **先 `Motor_Update()`，再 `EncoderUsart_Task()`**：否则读到的是上一次的累计值。
2. **先初始化 USART，再 `EncoderUsart_Init()`**：否则启动信息发不出去。
3. **先 `Motor_Init()`，再 `EncoderUsart_Init()`**：左右各调用一次，确保两路计数都已清零并开始累计。
4. **不要在中断里调用**：内部使用阻塞式 `HAL_UART_Transmit()`。
5. **串口发送会短暂阻塞主循环**：两个串口各发一行，约 26 ms；编码器累计仍在 `Motor_Update()` 中完成。
6. **默认静止时不输出**：看到启动信息后没有数据，先转一下车轮，或把 `ENCODER_USART_ALWAYS_PRINT` 改为 1。
7. **RPM 是窗口平均值**：不是瞬时转速，窗口由 `ENCODER_USART_PERIOD_MS` 决定。
8. **计数方向不对**：改 `motor.h` 里的 `MOTOR_*_ENC_INVERTED`，不要改本模块。
9. **串口只发不收**：当前没有 `r` 清零命令；需要清零可以调用 `Motor_ResetCount()`。

---

## 13. 常见问题

| 现象 | 可能原因 | 处理 |
|---|---|---|
| 完全没有输出 | USB-TTL RX 没接 PA9、没共地、波特率不对 | 检查 PA9→RXD、GND、115200 8N1 |
| 只有启动信息，之后没有数据 | 车轮没转，默认模式不发送 | 转一下车轮，或把 `ENCODER_USART_ALWAYS_PRINT` 改为 1 |
| 输出乱码 | 波特率/数据格式不对，或电平不匹配 | 检查 115200 8N1 和 3.3 V 电平 |
| `tot` 一直是 0 | 编码器没供电、A/B 没接、TIM 没启动 | 检查编码器 3.3 V、GND、PB6/PB7、PA0/PA1 |
| 正转时 `tot` 为负 | 编码器计数方向相反 | 改 `MOTOR_LEFT_ENC_INVERTED` 或 `MOTOR_RIGHT_ENC_INVERTED` |
| `rpm` 一直是 0 | 计数没有变化，或采样窗口内没有脉冲 | 检查编码器接线和 `Motor_Update()` 是否在调用 |
| `d` 偶尔为 0 但仍输出一行 | 停转后的最后一次采样 | 正常现象，之后不再发送 |
| 只初始化一路，`EncoderUsart_Task()` 不输出 | 只调用了一次 `EncoderUsart_Init()` | 左右各调用一次 |
| 圈数与预期差约 30 倍 | 每圈计数用错（52 还是 1560） | 确认本模块按车轮圈数计算，1560 计数/圈 |

---

## 14. 文件对应关系

| 内容 | 文件 |
|---|---|
| 编码器串口接口声明 | `Core/Inc/encoder_usart.h` |
| 编码器串口实现 | `Core/Src/encoder_usart.c` |
| 电机对象和编码器累计 | `Core/Inc/motor.h`、`Core/Src/motor.c` |
| 主循环调度 | `Core/Src/main.c` |
| USART / 编码器引脚配置 | `car.ioc` |
| 电机模块接口文档 | `doc/motor_api.md` |
| 本文档 | `doc/encoder_usart_api.md` |
