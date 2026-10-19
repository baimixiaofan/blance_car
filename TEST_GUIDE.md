# PWM 驱动波形与 TB6612 输出测试指导

适用硬件：STM32F103C8T6 + TB6612FNG，对应本工程 `car.ioc` 和《引脚配置.md》。

## 1. 已经完成的程序

- 新增 `Core/Inc/motor_test.h`、`Core/Src/motor_test.c`，直接配置 TIM1 和 GPIO，不依赖 CubeMX 生成的定时器代码。
- `PA8 / TIM1_CH1` → 右电机 PWMA，`PA11 / TIM1_CH4` → 左电机 PWMB。
- `PB14/PB15` → 右电机 AIN1/AIN2，`PB12/PB13` → 左电机 BIN1/BIN2。
- PWM 频率 20 kHz，上电先输出 0 占空比、方向线全低，1 s 后才开始动作。
- 自动测试：正转 30% → 60% → 90%，再反转 30% → 60% → 90%，每档 5 s，档间停止 0.5 s，循环运行。
- 当前默认 `MOTOR_TEST_USE_RIGHT_MOTOR=0`，**只驱动左电机**；右电机的引脚定义和代码都保留，恢复双电机测试时把该宏改成 `1` 即可。
- 已修正原工程把 SWD 关闭的问题（`stm32f1xx_hal_msp.c` 和 `car.ioc`），现在 PA13/PA14 可以正常下载调试。
- 已编译出烧录文件：
  - `build/homework/car.hex`
  - `build/homework/car.bin`
  - `build/homework/car.elf`

## 2. 上电前接线检查

| TB6612 引脚 | 接到 | 说明 |
|---|---|---|
| PWMA | PA8 | 右电机 PWM，3.3 V 逻辑 |
| AIN1 / AIN2 | PB14 / PB15 | 右电机方向 |
| PWMB | PA11 | 左电机 PWM，3.3 V 逻辑 |
| BIN1 / BIN2 | PB12 / PB13 | 左电机方向 |
| STBY | 高电平（5 V 或 3.3 V） | 必须为高，否则芯片待机、无输出 |
| VCC | 5 V | 逻辑电源 |
| VM | 电机额定电压（TB6612 最大 15 V） | 功率电源 |
| GND | 公共地 | MCU、TB6612、电机电源、示波器必须共地 |
| AO1 / AO2 | 右电机 | 通道 A |
| BO1 / BO2 | 左电机 | 通道 B |

注意事项：

1. **先抬起车轮再上电**，自动程序会让已使能的电机转动。
2. 用电池或带限流的电源给 VM 供电；两路同时 90% 占空比时电流较大，USB 5 V 很容易掉压复位。
3. 用万用表确认 STBY 对地电压 > 2 V。
4. 对照 PCB 网络名确认 `AIN1/AIN2`、`BIN1/BIN2` 没有接反。若某个电机标注的正反转与实际相反，把 `motor_test.h` 中对应的 `..._DIR_INVERTED` 改为 `1`，不需要改接线。
5. 示波器接地夹只夹**板子 GND**，绝对不能夹在 AO1/AO2/BO1/BO2 上，否则会把驱动输出短路到地。

本程序假定的方向定义：

| 状态 | PB14 / PB12 | PB15 / PB13 | 输出 |
|---|---|---|---|
| 正转 | 1 | 0 | AO1（BO1）输出 PWM，AO2（BO2）为低 |
| 反转 | 0 | 1 | AO2（BO2）输出 PWM，AO1（BO1）为低 |
| 停止 | 0 | 0 | 两输出关闭 |

## 3. 测试参数：改 `motor_test.h` 即可

| 宏 | 默认值 | 作用 |
|---|---:|---|
| `MOTOR_TEST_MODE` | 0 | 0=自动循环；1=固定状态 |
| `MOTOR_TEST_USE_LEFT_MOTOR` | 1 | 左电机使能；0=该路保持停止 |
| `MOTOR_TEST_USE_RIGHT_MOTOR` | 0 | 右电机使能；当前为 0，只测左电机 |
| `MOTOR_TEST_PWM_FREQUENCY_HZ` | 20000 | PWM 频率 |
| `MOTOR_TEST_STEP_MS` | 5000 | 每档保持时间 |
| `MOTOR_TEST_STEP_GAP_MS` | 500 | 档间停止时间 |
| `MOTOR_TEST_START_DELAY_MS` | 1000 | 上电后延时 |
| `MOTOR_TEST_DUTY_1/2/3_PERCENT` | 30/60/90 | 三档占空比 |
| `MOTOR_TEST_FIXED_LEFT/RIGHT_DIR` | FORWARD | 固定模式方向 |
| `MOTOR_TEST_FIXED_LEFT/RIGHT_DUTY` | 60/60 | 固定模式占空比 |
| `MOTOR_TEST_LEFT/RIGHT_DIR_INVERTED` | 0 | 方向与标注相反时改为 1 |

### 常用改法

**按作业要求恢复左右两路测试：**

```c
#define MOTOR_TEST_USE_LEFT_MOTOR      1
#define MOTOR_TEST_USE_RIGHT_MOTOR     1
```

**只拍右电机反转 60%、左电机停：**

```c
#define MOTOR_TEST_MODE                1
#define MOTOR_TEST_USE_LEFT_MOTOR      0
#define MOTOR_TEST_USE_RIGHT_MOTOR     1
#define MOTOR_TEST_FIXED_LEFT_DIR      MOTOR_DIR_STOP
#define MOTOR_TEST_FIXED_RIGHT_DIR     MOTOR_DIR_REVERSE
#define MOTOR_TEST_FIXED_LEFT_DUTY     0U
#define MOTOR_TEST_FIXED_RIGHT_DUTY    60U
```

方向可填 `MOTOR_DIR_STOP`、`MOTOR_DIR_FORWARD`、`MOTOR_DIR_REVERSE`、`MOTOR_DIR_BRAKE`。
被停用的一路方向线固定为低、占空比固定为 0，不会误转。

## 4. 编译和烧录

在工程根目录执行（也可以在 CLion 里直接 Build）：

```bash
cmake -S . -B build/homework -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build/homework
```

生成 `build/homework/car.hex` 后烧录。使用 STM32CubeProgrammer 命令行时：

```bash
"D:/ST/STM32CubeCLT_1.19.0/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe" -c port=SWD -w "E:/control/car/build/homework/car.hex" -v -rst
```

如果 ST-Link 连不上（板上原有程序可能关闭过 SWD），用 **Connect Under Reset** 方式连接后再烧录。

## 5. 自动测试时序

上电后约 1.5 s 开始第一档，从第一档开始到下一轮重新开始约 33 s：

| 相对上电时间 | 方向 | 占空比 |
|---:|---|---:|
| 1.5 ~ 6.5 s | 正转 | 30% |
| 7.0 ~ 12.0 s | 正转 | 60% |
| 12.5 ~ 17.5 s | 正转 | 90% |
| 18.0 ~ 23.0 s | 反转 | 30% |
| 23.5 ~ 28.5 s | 反转 | 60% |
| 29.0 ~ 34.0 s | 反转 | 90% |

每档结束后占空比先回 0，方向线保持到下一档开始前才切换，避免带载换向。

## 6. 示波器测量

### 6.1 测一路（2 通道示波器）

以右电机正转为例：

| 示波器 | 探头接点 | 建议档位 |
|---|---|---|
| CH1 | PA8（PWMA 输入） | 10×，DC，1 V/div |
| CH2 | AO1（右电机输出） | 10×，DC，5 V/div（VM=12 V 时也可 5 V/div 加偏移） |
| GND | 板子 GND | 两个探头共地 |

- 时基：`10 µs/div`，一屏约 5 个 50 µs 周期。
- 触发：CH1 上升沿、约 1.6 V、Normal 或 Single。
- 自动模式下看到需要的档位时按 Single；时间不够就改固定模式重拍。

测左电机把 CH1 接 PA11、CH2 接 BO1；反转时输出改测 AO2 / BO2。

### 6.2 预期波形（用来判断是否正常）

| 项目 | 输入侧（PA8/PA11） | 输出侧（AO/BO） |
|---|---|---|
| 频率 | 20 kHz（实测约 19.8~20.2 kHz，HSI 有 ±1% 误差） | 与输入相同 |
| 周期 | 约 50 µs | 约 50 µs |
| 占空比 | 30/60/90% | 与输入相同（误差 ≤1%） |
| 高电平 | 约 3.3 V | 约 VM（轻载时接近 VM，带载有 0.1~0.5 V 压降） |
| 低电平 | 约 0 V | 约 0 V |
| 相位 | — | 与输入同相，延迟在 µs 级以内 |
| 稳定性 | 三档频率、脉宽稳定 | 无丢波、无明显异常振铃、无发热 |

要点：TB6612 是**开关功率放大**，输入 3.3 V 逻辑 PWM，输出幅度由 VM 决定，不是把 3.3 V 线性放大到 12 V。正转时低边输出（AO2/BO2）为 0，反转时另一路输出为 0，这是正常的。

### 6.3 需要拍的照片清单（DC1 / DC2）

| 编号 | 内容 | 建议张数 |
|---|---|---:|
| DC1-右正转 | PA8 + AO1，30/60/90% | 3 |
| DC1-左正转 | PA11 + BO1，30/60/90% | 3 |
| DC2-右反转 | PA8 + AO2，30/60/90% | 3 |
| DC2-左反转 | PA11 + BO2，30/60/90% | 3 |
| 方向控制 | PB14/PB15 或 PB12/PB13，1 s/div 抓正转→反转 | 2 |

> 当前程序默认只驱动左电机。要拍右电机（DC1-右正转、DC2-右反转）的照片，先把 `MOTOR_TEST_USE_RIGHT_MOTOR` 改为 `1` 并重新编译烧录。

- 如果示波器有 4 个通道，可把 PA8、AO1（或 AO2）、PA11、BO1（或 BO2）同时接入，一张照片同时体现左右通道，每个方向拍 3 张即可。
- 每张照片上标注：`CH1=PA8 输入 3.3V`、`CH2=AO1 输出 VM`、`f=20kHz`、`D=30%`、`正转/反转`。照片里保留时基、垂直档位和测量值，老师要看这些。
- 方向线照片可在自动模式下用 `1 s/div`、Roll 模式，抓从正转切到反转的过程；只要能看到 AIN1/AIN2（或 BIN1/BIN2）一高一低互换即可。

## 7. 作业文档怎么填

### 7.1 直流电机部分

- **测试 PWM 频率**：`20000` Hz；**占空比**：`30 / 60 / 90` %
- **波形 DC1（正转）照片说明**：
  - 右通道：PA8(TIM1_CH1) → PWMA，AIN1(PB14)=1、AIN2(PB15)=0，AO1 为 PWM，AO2 为低。
  - 左通道：PA11(TIM1_CH4) → PWMB，BIN1(PB12)=1、BIN2(PB13)=0，BO1 为 PWM，BO2 为低。
  - 三档占空比 30%/60%/90%，输入为 3.3 V PWM，输出幅度约等于 VM，频率和占空比与输入一致，波形稳定。
- **波形 DC2（反转）照片说明**：
  - 右通道：AIN1(PB14)=0、AIN2(PB15)=1，AO2 为 PWM，AO1 为低。
  - 左通道：BIN1(PB12)=0、BIN2(PB13)=1，BO2 为 PWM，BO1 为低。
  - 三档占空比同上，频率、占空比与输入一致。
- **结论可以这样写**：

> 经示波器测量，MCU 输出 PWM 频率为 20 kHz，正转和反转时 TB6612 输出频率、占空比均与输入一致，三档占空比（30%/60%/90%）下波形稳定，无丢波和异常发热；输出幅度由电机电源 VM 决定，方向控制线电平与电机转向对应，驱动链路工作正常。

### 7.2 交流电机部分

本组设计是直流电机方案，交流部分没有做，可以写：

> 本组采用直流电机驱动方案，未使用交流电机驱动模块，故第二部分不适用。

### 7.3 异常位置及复测方案

如果老师要求写这一栏，而直流部分测试正常，可以写：

> 直流电机测试各测量点波形均正常，未发现异常。复测时按“先测 MCU 侧 PA8/PA11，再测驱动侧 AO1/AO2、BO1/BO2，最后核对方向线电平”的顺序逐点排查；若出现无输出，优先检查 STBY 是否为高、VM 是否供电、共地是否可靠。

## 8. 常见问题

| 现象 | 检查 |
|---|---|
| 电机完全不转 | STBY 是否高；VM/VCC 是否有电；共地；程序是否已下载 |
| 只有一个电机转 | 对应通道的 PWM/方向线是否接对；PA11 是否被别的功能占用 |
| 电机转向和标注相反 | 把 `MOTOR_TEST_LEFT_DIR_INVERTED` 或 `MOTOR_TEST_RIGHT_DIR_INVERTED` 改为 1 |
| 输出幅度明显低于 VM | VM 电源限流/电池电量低/线损；带载时 MOSFET 压降属正常 |
| 输出频率不是 20 kHz | 先测 PA8/PA11；程序会按 PCLK2 自动计算 ARR，若改过时钟仍然应是 20 kHz |
| 波形边沿振铃严重 | 示波器地线太长，用短地弹簧；输出端可并小电容/ RC 吸收，但不要影响驱动 |
| 下载器连不上 | 用 Connect Under Reset；本工程已把 SWD 重新打开 |

如果后面要在 CubeMX 里正式配置 TIM1 和这些 GPIO，需要删掉 `motor_test.c` 里的直接寄存器初始化，避免和 CubeMX 生成的初始化重复。
