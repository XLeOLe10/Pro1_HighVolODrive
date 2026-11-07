# STM32H750 当前移植基线

> **历史移植说明／兼容入口（2026-11-06快照）**：保留原路径与当时说明，不再同步维护当前配置表。日常先读[docs/README](docs/README.md)，当前实现唯一维护在[ARCHITECTURE](docs/ARCHITECTURE.md)，未来方案见[003](docs/specs/003-c-port-architecture.md)。以下“当前”均指本快照，具体参数以源码/.ioc及架构核对版本为准。

## 原说明快照

本文核对基线为 `1668486`（2026-11-06，提交说明“接入RTOS，keil测试通过”），用于说明实际硬件配置和业务职责。后续实现与验收记录见 [RTOS spec](docs/specs/001-rtos-preflight.md)；本文件中的“已实现”不等于所有实板场景已经通过。

历史：工程由 STM32G474 的开环角度/电流反馈程序迁入 STM32H750VBT6，随后加入旋变反馈、电流环、速度电流双环及 RTOS。旧版文档中的 ADC1 Rank2 读取 W、6 列/20ms 遥测、校准后自动拉高 EN、固定 0.08 电压/10rpm/s 斜坡/100rpm 目标，均已不代表当前实现。

## 1. 时间与执行上下文

| 功能 | 当前配置 / 执行者 | 源码依据 |
| --- | --- | --- |
| CPU / SYSCLK / HCLK | 200MHz；VOS3、Flash 4 等待周期 | `Core/Src/main.c`、`FOC_T.ioc` |
| TIM1 控制 | 定时器时钟 200MHz；PSC=1、ARR=2499、中心对齐模式1、RCR=1 | `Core/Src/tim.c` |
| ADC 触发 | TIM1 CH4，PWM2、CCR4=1250；TRGO2=OC4REF，上升沿触发注入组 | `Core/Src/tim.c`、`Core/Src/adc.c` |
| FOC / 电流反馈 | TIM1 update ISR → `AppRuntime_FastISR()` | `Core/Src/main.c`、`Core/Src/app_runtime.c` |
| 速度环与模式处理 | TIM1 中 20 分频调用 `OpenFOC_Ramp_1ms()` | `Core/Src/app_runtime.c`、`Core/Src/open_foc.c` |
| HAL 毫秒时基 | TIM7，主时钟下 PSC=199、ARR=999，1kHz | `Core/Src/stm32h7xx_hal_timebase_tim.c` |
| RTOS 毫秒时基 | SysTick，1000Hz；CMSIS-RTOS2 包装 FreeRTOS | `Core/Inc/FreeRTOSConfig.h`、CMSIS_RTOS_V2 |
| 后台任务 | 一个业务任务 `appTask`，入口 `StartAppTask`，`osPriorityNormal`，按 RTOS tick 每 1ms 调度 | `Core/Src/freertos.c` |
| 后台职责 | 旋变采集、PWM 通道准备、每 5ms 尝试发送遥测、每 1000ms 更新内存诊断 | `Core/Src/app_runtime.c` |

“20kHz”是工程中的标称频率。按当前中心对齐完整上/下计数周期，`T = 2 × ARR × (PSC+1) / 200MHz = 49.98μs`，对应约 `20.008kHz`；当前 ISR 统计预算也取 `9996` 个 CPU cycles。20 分频处理的实际间隔约 `0.9996ms`，与 RTOS 任务的 1ms 是两条不同的时基。

原控制算法保留 `FOC_CURRENT_TS=0.00005f` 和 `FOC_SPEED_TS=0.001f`，与上述配置周期约有 0.04% 差异。旋变差分测速则已使用实际 DWT 采样间隔。本阶段只说明差异，不修改定时器或 PI 参数；精确频率、触发位置和输出更新截止点仍需波形验证，不能仅凭函数名或 Watch 计数认定严格 20kHz。

TIM1 死区配置 DTG=100、CKD=DIV1，按 200MHz 时钟折算约 500ns；真实上下管驱动波形与有效死区尚未验收。当前 TIM1 Break/Break2 均关闭，不能把软件故障锁存当作已具备硬件过流关断。

NVIC 使用 `NVIC_PRIORITYGROUP_4`，子优先级均为 0：

| 中断 | 抢占优先级 |
| --- | ---: |
| TIM1 update | 0 |
| TIM7 | 4 |
| USART1 | 5 |
| SysTick / PendSV | 15 |

FreeRTOS 系统调用门槛为 5。TIM1 和 TIM7 不调用 RTOS API；TIM1 不等待任务、SPI 或 UART 完成。跨任务/TIM1 的旋变及遥测副本使用短 PRIMASK 临界区；普通 FreeRTOS 临界区不能屏蔽优先级 0 的 TIM1。

## 2. 当前引脚与 ADC 映射

| 功能 | H750 引脚 / 外设 |
| --- | --- |
| U / V / W 高侧 PWM | PA8 / TIM1_CH1，PA9 / TIM1_CH2，PA10 / TIM1_CH3 |
| U / V / W 低侧 PWM | PE8 / TIM1_CH1N，PE10 / TIM1_CH2N，PB1 / TIM1_CH3N |
| 公共门极驱动 EN | PB10，GPIO，高有效 |
| U 相电流 | PA0 / ADC1 CH16，注入 Rank1，读取 ADC1 JDR1 |
| V 相电流 | PA5 / ADC2 CH19，注入 Rank1，读取 ADC2 JDR1 |
| W 相电流 | PC0 / ADC3 CH10，注入 Rank1，读取 ADC3 JDR1 |
| 母线电压 | PC3_C / ADC3 CH1，注入 Rank2，读取 ADC3 JDR2 |
| USART1 TX / RX | PB6 / PB7，AF7 |
| SPI1 SCK / MISO / MOSI | PB3 / PB4 / PB5，AF5 |
| AD2S1210 SAMPLE / A1 | PB8 / PB9 |
| AD2S1210 A0 / WR | PE0 / PE1 |

ADC1/2/3 注入组均由 TIM1 TRGO2 上升沿启动。U/V/W 在各 ADC 的首个 rank 采集，母线电压在 ADC3 的第二个 rank 随后采集；这不是 U/V/母线同时采集、W 随后的旧方案。实际跨 ADC 采样偏差仍需验证。

三路 ADC 都为 12bit、异步时钟 `per_ck=64MHz` 除以 4，即 16MHz；注入采样时间 64.5 cycles。PC3_C 使用独立模拟通路，源码将 PC3 模拟开关保持 OPEN。三个 ADC 的 JEOS 都就绪时才由 `CurrentSense_GetRaw()` 取得四个值；未就绪立即返回并由调用方记录缺帧，不在 TIM1 内自旋等待。

`CurrentMonitor_Run_20kHz()` 先等待 2000 帧，再累积 4096 帧求电流零偏；`monitor_ready=1` 表示这项流程完成。当前控制用 U 相电流由 `-(V+W)` 重构，PA0 的实测 U 相值保留为诊断量。以上是代码现状，不能代替传感器量程、比例、极性和噪声的实板校验。

SPI1 为 Mode 1（CPOL=0、CPHA=第二沿）、8bit、MSB first、软件 NSS，64MHz/256=250kHz。SAMPLE/A1/A0/WR 的 GPIO 初值均为高，AD2S 初始化中再按事务切换。RESET 未接 MCU，代码实际执行上电等待、配置及故障读清/确认；`AD2S1210_REG_SOFT_RESET` 仅有定义，没有软件复位寄存器写入。SPI 由 appTask 正常独占；TIM1 读取已发布的角度/速度快照。

## 3. 当前控制模式与遥测

`FOC_ControlState` 的 0/1/2 表示控制模式，不能单独表达整机的初始化、待机、运行、故障等生命周期。

| 值 | 模式 | 当前算法行为 |
| --- | --- | --- |
| 0 | `FOC_CONTROL_CURRENT_LOOP` | 使用旋变角度，闭合 d/q 电流环；q 轴电流取调试给定 |
| 1 | `FOC_CONTROL_SPEED_CURRENT_LOOP` | 速度 PI 产生 q 轴电流给定，再进入电流环 |
| 2 | `FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST` | 用命令生成旋转角度和 q 轴归一化电压，观察旋变反馈与极对数估计 |

当前启动默认 `foc_run_request=0`、模式 0、q 轴电流给定 0A；应用初始化设置速度目标 50rpm、开环试验归一化 Uq=0.02。斜坡代码每个标称 1ms 步进 0.20rpm（名义 200rpm/s），模式 0/1 启动有约 1200 个慢环拍的对齐阶段，模式 2 新启动跳过对齐。它们是现有调试参数，不是本项目经过完整验证的运行限值。

模式 2 的控制角度不由旋变角度闭环决定，但当前 app_runtime 的输出许可仍要求有效且新鲜的旋变位置/速度，以及本拍新 ADC 帧；不能以“开环试验”为理由跳过此集成层条件。

USART1 为 115200、8N1。appTask 每 5ms 尝试发送两列 CSV：`速度参考rpm,速度反馈rpm\r\n`，三位小数。采用 `HAL_UART_Transmit_IT` 和任务独占的静态缓冲区；忙时计入 `current_telemetry_drop_count`，不覆盖正在发送的缓冲区。当前不是六列/20ms 格式，也不以电流零偏完成作为开始遥测的前提。

## 4. 启动、输出所有权与故障

1. `main` 早期使 EN 失效，功率 PWM 引脚先保持 GPIO 低。生成外设初始化结束后，应用初始化再次恢复该状态，完成 ADC 硬件校准、旋变初始化和控制参数初始化。
2. 旋变初始化必须真实通过配置回读及 DWT 检查；失败会锁存故障，不再强制把 initialized 设为成功。
3. 内核和 appTask 创建成功、调度运行后，先启动三组 ADC，再启动 CH4 触发源和 TIM1 update 中断。保持功率禁止时仍要能采样。
4. 电流零偏完成后，appTask 只准备六路 PWM 通道和引脚、发布 `power_pwm_prepared`。HAL 准备调用可能改变 MOE，因此不能仅凭代码顺序声称整个过程的物理 PWM 电平已经验证；EN 在准备期间维持失效。
5. 正常运行时只有 TIM1 在短 PRIMASK 区最终决定允许输出：运行请求有效、通道已准备、零偏 ready、位置/速度有效且年龄不超过当前 2ms 预算、本拍 ADC 帧有效、没有锁存故障、没有试验锁定。满足条件后才置 MOE、拉高 EN。**零偏完成不会自动启动电机。**
6. 准备完成前的启动请求被清除并计入 `app_start_rejected_count`，就绪后需要新的请求。正常撤销运行请求会关闭 EN/MOE；这不是已实现受控减速停车的证明。
7. 故障锁存立即撤 EN/MOE，另清 CH1–3 的六个 CCER 使能，保留 CH4/计数器以维持诊断采样。初版故障只允许复位恢复，不能再次写运行请求绕过。fatal 路径不依赖 RTOS 或 HAL 等待。

`BOARD_POWER_STAGE_MODE` 当前为 `BOARD_POWER_STAGE_GPIO_EN`。此 RTOS 集成只支持 GPIO_EN 或完全禁用输出；`ALWAYS_ON` 会被 `board_safety.c` 编译拒绝，需要独立启动设计，不能照旧注释当成受支持配置。

旋变 SPI 事务/速度有效性/反馈年龄参与软件运行许可；AD2S 设备自身故障位目前仍用于诊断。母线、过流、温度、失联等完整保护状态机尚未完成，TIM1 Break 也未配置启用。

## 5. RTOS 内存和已有证据

FreeRTOS 使用 heap_4，堆 16384B；appTask 动态创建，栈 1024 words=4096B，栈溢出检查=2，malloc 失败钩子开启。除唯一业务任务外，内核仍有 Idle/Timer 任务；不能把“一个业务任务”理解成系统只有一个线程。

1668486 基线的全量 Keil 构建记录为 0 errors、0 warnings；Flash ROM 59576B，RW+ZI 25336B，分配在 AXI RAM。appTask 的动态栈来自 RTOS 堆，不应在静态 RAM 总量之外再次加算。构建日志和指纹见 [验证记录](docs/validation/001-rtos-integration-build.txt)。

用户在功率母线断开、仅控制与传感器供电时提供的 Watch 观察支持基础启动初检通过：任务/采样累计量相符，未锁存故障；appTask 最低剩余栈 3616B、RTOS 最低剩余堆 12168B，停止状态下记录的控制函数最大耗时 22.05μs；ADC 缺帧和旋变采样间隔异常为 0，反馈无效计数在启动增至 22 后按用户观察稳定。完整读数及边界见 RTOS spec 第15节。

这些结果不覆盖最坏任务路径、MSP 中断栈、完整 IRQ 响应延迟和真实闭环负载；运行中 Watch 也不是跨变量原子快照。不能据此缩减栈或宣称功率级已经通过验收。

## 6. 后续验证入口

保持既定母线隔离条件，可继续记录不中断运行的计数增量、旋变故障位、反馈/ADC 诊断和内存余量；比较两套 tick 的增量而不是起始值。需要暂停/单步时，之后的时序验收应从新的连续运行区间重测。PB3 已用于 SPI1 SCK，调试测量需避免占用此引脚作为 SWO。

以下仍未完成：

- EN/PWM 在上电、复位、准备、停止、故障时的实际电平及关断延迟；MOE 关闭时 ADC 触发波形与相对时刻。
- 电流/母线量值、极性、零偏精度和旋变角度/转速方向，设备故障位策略。
- 最坏控制耗时、完整 IRQ 响应延迟、最长关中断时间、MSP 和各任务最坏栈余量。
- 真实故障路径和原有闭环功能的受控验证；外部硬件保护及再生能量处理。

硬件测试需要按实际板卡、供电、限流、负载和测量条件安排。本文纠正文档事实，不改变外设配置、控制算法、保护阈值，也不代表授权直接接通功率母线。
