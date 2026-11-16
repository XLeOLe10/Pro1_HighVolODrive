# 当前架构：STM32H750 纯 C 控制程序

本页只维护**已经实现的当前架构**；拟议机制留在 spec。入口见 [docs/README.md](README.md)，移植路线见 [003 第7节](specs/003-c-port-architecture.md#7-移植表与顺序)。目标是参考 ODrive 的组织与算法，用 C 实现，不接入其 C++ 对象体系。

版本锚点：2026-11-16，`Xiao_ODrive`，固件基线`350ef94`（007已提交）＋008只读查询工作区增量；008 SRC/HOST/两目标增量BUILD通过，STATUS正确回包与四项计数0支持HW有限通过（BUILD/FAULT回包未留图），见[008](specs/008-can-command-protocol.md)。007加入Bench CAN轮询PING/PONG，SRC/HOST/BUILD通过；用户500 kbit/s、1 Hz的01/02回显与四项异常计数全0截图支持HW有限通过，详情与证据边界见[007第7节](specs/007-can-link-bringup.md)。006旧有限禁输出截图留在[006记录](validation/006-axis-run-phase-20261110.txt)，不自动支持本轮CAN/VOS1新固件。

本工程有两个构建目标：`FOC_T`正常配置（未确认范围，拒绝START/UPDATE）；`FOC_T_CommandBench`以编译宏强制禁功率输出，合成命令只进入同一交接核心，FOC始终停机。测试目标有一次自动生产者，正常目标没有。当前验收与板测证据入口见[007](specs/007-can-link-bringup.md)，命令机制及旧演练见[005](specs/005-command-handoff.md)。

003旧AXF差异原因仍未知；原发现及当时重建保留在[原记录](validation/003-architecture-build.txt)。当前验收层级逐项查[007](specs/007-can-link-bringup.md)。

## 源码分类导航

这是现有文件的职责分类，**不是新建目录或移动文件**。手写代码目前仍在Core/Src与Core/Inc；Keil两个目标仍使用Application/User/Core等既有工程组。main/freertos与外设文件带CubeMX生成骨架，用户逻辑入口和生成代码边界仍须保留。

同名.c/.h作为一个模块看待：.c是实现，.h是对外接口；只在本模块内使用的辅助函数留在.c，不再单独拆头文件。

| 分类 | 现有文件入口（同名.h在Core/Inc） | 以后什么改动放这里 |
| --- | --- | --- |
| 启动与调度 | [main.c](../Core/Src/main.c)、[freertos.c](../Core/Src/freertos.c)、[app_runtime.c](../Core/Src/app_runtime.c)、[app_policy.h](../Core/Inc/app_policy.h)、[FreeRTOSConfig.h](../Core/Inc/FreeRTOSConfig.h) | 启动、任务节拍、实时/后台调用、运行门禁组合 |
| 控制命令、状态与算法 | [axis_command.c](../Core/Src/axis_command.c)、[axis_command_core.c](../Core/Src/axis_command_core.c)、[axis_status.c](../Core/Src/axis_status.c)、[open_foc.c](../Core/Src/open_foc.c) | 命令交接/同步、纯C许可规则、状态观察、FOC计算；状态观察不授予许可 |
| 采样与反馈 | [ad2s1210.c](../Core/Src/ad2s1210.c)、[current_sense.c](../Core/Src/current_sense.c)、[current_monitor.c](../Core/Src/current_monitor.c) | 旋变读取、电流/母线读取与换算、反馈有效性 |
| 板级配置与安全 | [board_config.h](../Core/Inc/board_config.h)、[board_safety.c](../Core/Src/board_safety.c) | 功率模式、EN/MOE管理、锁存故障与关断 |
| 通信与报文 | [can_link.c](../Core/Src/can_link.c)、[can_link_protocol.c](../Core/Src/can_link_protocol.c)、[telemetry_format.c](../Core/Src/telemetry_format.c) | CAN有界收发、字节编解码、串口遥测格式化；新增查询优先扩展现有文件 |
| 外设与芯片支持 | [adc.c](../Core/Src/adc.c)、[tim.c](../Core/Src/tim.c)、[spi.c](../Core/Src/spi.c)、[usart.c](../Core/Src/usart.c)、[fdcan.c](../Core/Src/fdcan.c)、[gpio.c](../Core/Src/gpio.c)、Core中stm32h7xx/system_stm32h7xx文件 | CubeMX生成配置、中断入口、HAL时基与芯片初始化；不混入协议/控制业务 |
| 验证代码 | [axis_command_bench.c](../Core/Src/axis_command_bench.c)；[tests](../tests)下的主机测试、桩与运行脚本 | Bench演练与HOST回归；tests不参与板端固件链接 |
| 底层库 | [Drivers](../Drivers)、[Middlewares](../Middlewares) | HAL/CMSIS/FreeRTOS等外部库；不把应用新功能塞进库文件 |

CAN保留两个职责清楚的模块：can_link处理硬件收发与限量服务，can_link_protocol处理字节格式，后者可直接在电脑测试。新增一条查询不对应新增一套.c/.h。axis_command_core与axis_command同理，前者管纯规则，后者管任务/ISR交接。

本轮008没有新增C源或头文件：修改5个已有固件文件、4个已有测试文件；新增4项仅为008 spec、验证记录和两张原始截图。后续同一功能继续更新这些入口。文件归类不改变运行职责，具体调用/时序仍以下文为准。

## 启动与执行上下文

```mermaid
flowchart LR
    M[main 初始化] --> K[创建 appTask / 启动内核]
    K --> A[appTask 约 1 kHz]
    K --> IDLE[内核空闲任务 / 可被抢占]
    IDLE -->|约1秒一次 显式appTask句柄| STK[栈水位 / 次数 / 完成时戳]
    A -->|SAMPLE + 阻塞 SPI1| R[AD2S1210]
    R -->|SPI返回| A
    A -->|发布32 B快照| S[旋变发布副本]
    T[TIM1计数器] --> P[CH4 / TRGO2]
    P -->|上升沿| ADC[ADC1 / ADC2 / ADC3 注入组]
    T -->|update IRQ| I[TIM1 FastISR 约 20 kHz]
    ADC -->|JDR + JEOS| I
    S -->|有界复制 / 年龄检查| I
    A -->|仅Bench自动演练 / PRIMASK固定发布| Q[32 B单槽命令 / 独立STOP票据]
    Q -->|TIM1边界消费| BND[命令核心 / epoch / 有效期]
    I --> BND
    BND -->|普通目标有效许可时安装命令| C[FOC / CCR1-3]
    BND -->|Bench强制空命令| OFF[FOC停机 / 禁功率输出]
    I --> B[board_safety 最终许可]
    F[异常 / 故障入口] -->|先撤输出| B
    I -->|两列遥测副本| A
    A -->|TX_IT| U[USART1 IRQ]
    A -->|仅Bench 采样后每轮最多1帧| CAN[CAN PING/PONG与只读查询]
    CAN <-->|FDCAN1 专用Message RAM 无IRQ/DMA| PC[USB-CAN电脑]
    I -->|诊断镜像| OBS[状态 / 许可 / 输出观察]
    F -->|故障锁存| OBS
    OBS -->|查询时短PRIMASK复制| CAN
```

命令箭头在测试目标由AxisCommandBench_Step实际驱动；正常目标仍无请求生产者/真实RX，范围未确认，拒绝START/UPDATE。测试目标只演练交接，BND→FOC路径被替换为ApplyCommand(NULL)。图中的 `board_safety` 是共享安全入口，不是独立任务；HAL 启动也会写 MOE/CEN，见下方完整写者表。

1. [启动文件](../MDK-ARM/startup_stm32h750xx.s) `Reset_Handler:237`：`ExitRun0Mode → SystemInit → __main`，C 运行库初始化静态对象后进入 `main`。
2. [main.c](../Core/Src/main.c) `main:74`：`MPU_Config → HAL_Init → BoardSafety_InitSafe → SystemClock_Config/PeriphCommonClock_Config`。首次软件撤输出在 HAL 初始化之后，不能证明复位到此时的引脚电平。
3. 同一 `main:104–113`：初始化 GPIO、TIM1、ADC1/2/3、SPI1、USART1、FDCAN1，再调用 [app_runtime.c](../Core/Src/app_runtime.c) `AppRuntime_Init:215`，恢复功率 GPIO 低、校准 ADC、初始化监测/旋变/FOC和命令核心；初始化失败进入 Fatal。
4. `main:117–121` 调用 `osKernelInitialize → MX_FREERTOS_Init → osKernelStart`；[freertos.c](../Core/Src/freertos.c) `MX_FREERTOS_Init:139` 只建一个业务任务 `appTask`，检查创建结果。内核返回走 Fatal，不回裸机循环。
5. 普通目标的 `StartAppTask:188 → AppRuntime_TaskStart:237`：启动三组 ADC、启动 CH4、再次撤 MOE，最后启动 TIM1 update IRQ。`CurrentMonitor_Run_20kHz` 消费 2000 帧等待加 4096 帧零偏标定后，appTask 才准备六路 PWM/AF 引脚。
5a. 测试目标：MX_TIM1_Init在USER区域跳过能配置AF的post-init调用，保持六路GPIO低；TaskStart在周期采集前调用CanLink_Init并初始化自动命令演练，然后直接只置CC4E，再Base_Start_IT，避免HAL启动CH4置MOE；不执行PreparePowerPWM。普通目标保留既有HAL路径。
6. `AppRuntime_FastISR` 取得反馈有效性后、任何慢环/FOC计算前，经 `AxisCommandPort_BeginCycle` 接收整组命令。最终短临界区调用 `AxisCommandPort_FinalPermit`，复核许可/期限/故障和原PWM准备/标定/反馈条件，才调用 `BoardSafety_SetOutputsEnabled`。旧 `foc_run_request` 是镜像；普通目标配置未确认，因此没有运行许可；测试目标的命令许可不等于功率许可。STOP是撤EN/MOE，不是受控减速。

## 模块分工与实时链

| 模块 / 职责 | 当前上下文与边界 | 函数 / 符号依据 |
| --- | --- | --- |
| 采样触发 | TIM1 CH4 的 OC4REF 经 TRGO2 上升沿触发 ADC 注入组；CPU 不逐次软件启动 | [tim.c](../Core/Src/tim.c) `MX_TIM1_Init:30`；[adc.c](../Core/Src/adc.c) `MX_ADC1/2/3_Init` |
| 快速入口 | TIM1 update IRQ → HAL 回调 → FastISR；不调用 FreeRTOS API | [stm32h7xx_it.c](../Core/Src/stm32h7xx_it.c) `TIM1_UP_IRQHandler:168`；`main.c HAL_TIM_PeriodElapsedCallback:254`；`app_runtime.c AppRuntime_FastISR:228` |
| 电流 / 母线 | 三个 JEOS 全到才消费，未到立即返回；U=ADC1/JDR1，V=ADC2/JDR1，W=ADC3/JDR1，母线=ADC3/JDR2 | [current_sense.c](../Core/Src/current_sense.c) `CurrentSense_HasNewSample:49/GetRaw:64`；`current_monitor.c CurrentMonitor_Run_20kHz:76` |
| 角度 / 速度 | appTask 每 1 tick 尝试 SAMPLE/SPI；TIM1 只拿已发布快照，不做 SPI | [ad2s1210.c](../Core/Src/ad2s1210.c) `AD2S1210_Run_1kHz:798/GetSnapshot:139`；`app_runtime.c BackgroundStep:200` |
| 命令与许可 | appTask唯一发布；TIM1唯一消费/active/epoch；局部校验、固定PRIMASK发布；默认配置未确认 | [axis_command.c](../Core/Src/axis_command.c) `AxisCommandPort_*`；[核心](../Core/Src/axis_command_core.c) `Validate/PublishValidated/Tick/Guard` |
| 板上命令演练（仅测试目标） | appTask单轮START/UPDATE/STOP/旧代拒绝/到期/无复活；读取同一GetSnapshot；故障/超时锁FAIL | `axis_command_bench.c AxisCommandBench_Init/Step`；`app_runtime.c BackgroundStep` |
| 原控制状态与FOC阶段 | TIM1每20拍慢环处理对齐/速度PI，再按拍执行电流控制；只读Getter从私有command_run/align_ms_left派生IDLE/ALIGNING/CLOSED_LOOP，无第二阶段权威 | [open_foc.c](../Core/Src/open_foc.c) `OpenFOC_GetRunPhase/ApplyCommand/Ramp_1ms/Run_20kHz` |
| 安全 / 只读状态 | board_safety 撤输出、锁存；axis_status 只汇报，不拥有启动权或完整 Axis 生命周期 | [board_safety.c](../Core/Src/board_safety.c) `BoardSafety_LatchFault:88`；[axis_status.c](../Core/Src/axis_status.c) `AxisStatus_Publish:5/LatchFault:12` |
| 遥测 / 堆监测 | appTask约5 ms尝试有界整数转换的两列速度CSV，USART1 IRQ搬运；约1000 ms查询堆；实时有效性检查仍在TIM1 | `app_runtime.c SendTelemetry/BackgroundStep/FastISR`；`telemetry_format.c TelemetryFormat_SpeedCsv` |
| 栈水位诊断 | 已有Idle任务约1000 ms扫描appTask栈，业务任务和IRQ可抢占；不等待/分配/关调度 | `freertos.c vApplicationIdleHook`；本地`tasks.c uxTaskGetStackHighWaterMark/prvTaskCheckFreeStackSpace` |
| CAN只读服务 | 仅Bench在appTask轮询，每轮最多1收/1次回包入队；PING/PONG和INFO/STATUS/FAULT/BUILD，不发布电机命令 | `can_link.c CanLink_Init/Poll/QueryValue`；`can_link_protocol.c`；`app_runtime.c ReadCanStatus` |
| 不存在的业务模块 | 无电机通信命令解析、独立通信/状态/日志任务或持久化日志；USART TX_RX 配置不代表已有 RX 业务 | [usart.c](../Core/Src/usart.c) `MX_USART1_UART_Init:31`；`freertos.c MX_FREERTOS_Init` 的队列/互斥锁等业务区为空 |

FreeRTOS 自带 Idle 和 Timer 服务任务不是额外业务任务，依据 `tasks.c vTaskStartScheduler`、`timers.c xTimerCreateTimerTask`。目前不因功能名称增加线程。

电流换算由 [current_monitor.c](../Core/Src/current_monitor.c) `CurrentMonitor_Run_20kHz:128–145` 完成：控制使用 `U=−(V+W)`，真实 U 仅作诊断；Clarke/Park 使用传入的 `foc_theta`。TIM1 中先更新旋变反馈、分频慢环，再消费电流、运行 FOC、写 PWM，最后复核输出许可并发布状态/遥测。

## 数据交接、生命周期与时间对应

下表里的静态数据位于当前 AXI SRAM 链接区域；任务局部副本位于 appTask 栈，ISR 局部量使用中断栈。`volatile` 只声明访问特性，不能让多字段更新自动变成一笔完整交易。

| 数据 / 单位 | 写者 → 读者；生命周期 | 已实现的交接 / 失效处理 | 位置依据 |
| --- | --- | --- | --- |
| 整组速度命令 / rpm、A、HAL ms | appTask局部副本→32 B单槽→TIM1私有active；静态AXI SRAM | Publish前区外校验；短PRIMASK内复核动态元数据/固定复制/DMB；空槽才接受。TIM1入口消费、末尾再复核 | `axis_command.c AxisCommandPort_Submit/BeginCycle/FinalPermit`；`axis_command_core.c` |
| 运行许可 / STOP / fault | TIM1拥有active/epoch，任务发独立STOP票据，board_safety随时关断 | STOP/到期/ready丢失/故障作废pending与active；旧epoch拒绝；故障先关硬件，软件代次在边界撤销。无自动恢复 | `AxisCommand_Tick/Revoke/Guard`；`BoardSafety_LatchFault` |
| 控制输入 / 调试镜像 | 普通目标TIM1把已验证active安装至FOC私有command变量；测试目标始终传NULL；原目标/run/请求模式/限制/step仅镜像 | `OpenFOC_ApplyCommand`整组安装，`OpenFOC_Stop`立即复位且允许未来明确START；`EnforceCommandLimit`每快拍限三个Iq指令/积分量 | `open_foc.c`；当前范围未确认，所有真实START/UPDATE被拒绝 |
| PI/校准参数 | 仍是现有调试/初始化量，未纳入命令合同 | 本轮只接管速度命令字段；不要并发修改PI/方向/零位等参数，后续另设配置合同 | `open_foc.c`原PI/对齐参数 |
| 命令确认/诊断 | 接受seq由Submit返回；TIM1记录applied_seq/control_seq/result_seq/cancelled_seq/stop_applied | GetSnapshot提供72 B一致副本，供appTask验收；Watch仍是独立标量。无实际协议/可靠ACK队列。control_seq模回绕只作观测，seq/epoch不回绕 | `axis_command.h/.c`与`axis_command_core.h` |
| 旋变角 rad、速度 rpm、双时戳、序号及有效性 | appTask → TIM1/任务；最新固定 32 B 静态快照，读者持有副本 | 发布/读取均保存 PRIMASK、屏蔽中断、固定复制、DMB、恢复；失败也递增尝试序号并发布无效帧；不在临界区做 SPI | `ad2s1210.h AD2S1210_FeedbackSnapshot`；`ad2s1210.c PublishSnapshot:109/GetSnapshot:139/Run_1kHz:811` |
| ADC 电流 / 母线原始码及换算值 A/V | ADC 硬件 JDR → TIM1；寄存器会被后续转换覆盖 | JEOS 检查后顺序读四个 JDR；无源触发帧号/采样时戳，软件消费计数不保证硬件同帧；运行中缺帧撤输出锁存 | `current_sense.c:49/64`；`current_monitor.c:98`；`app_runtime.c:297–308` |
| 积分器、dq 给定、占空比 | main 给初值，运行期间 TIM1 控制代码独占 | 同一执行上下文；后台尚无正式修改接口；板级故障独立覆盖输出 | `open_foc.c OpenFOC_Init/Ramp_1ms/Run_20kHz/FOC_WriteDuty` |
| CAN PING/只读查询 | FDCAN RX→appTask栈副本→HAL复制TX RAM；644 B专用RAM，独占FDCAN1 | BLOCKING单消费者，DLC先检查再HAL复制；无DMA/cache维护；Bus-Off锁停服务，不改控制许可 | `can_link.c CanLink_Init/Poll/PollErrors`；[008](specs/008-can-command-protocol.md) |
| CAN诊断副本 | TIM1/故障入口的5个镜像→appTask局部20 B→单帧结果 | 仅合法STATUS/FAULT触发ReadCanStatus；保存/恢复PRIMASK，固定读取，区外编码；不保证同拍或当前许可，多个请求各自采样 | `app_runtime.c ReadCanStatus`；`can_link.h CanLinkReadStatus`；[008第6节](specs/008-can-command-protocol.md) |
| 两列速度遥测 | TIM1 → appTask局部副本 → USART1 IRQ；静态发送缓冲96 B | 短PRIMASK复制两列，区外有界格式化；UART忙则丢帧、不覆盖在发缓冲；无DMA | `app_runtime.c SendTelemetry/FastISR`；`telemetry_format.c TelemetryFormat_SpeedCsv` |
| app_stack_min_free_bytes / probe_count / last_tick_ms | Idle Hook唯一写入 → 调试观察；32位静态标量，appTask不消费 | 显式稳定appTaskHandle；words转bytes，保留历史最小值，完成后计时；UINT32_MAX/count=0为未测。三项不是原子快照，不进入控制许可 | `freertos.c vApplicationIdleHook`；`app_runtime.c`定义与`app_runtime.h`说明 |
| axis_status_word / 板级故障 | TIM1正常发布、故障入口置位 → 调试读取；32位观察量 | TIM1用私有阶段Getter判断对齐，仍在最终输出后发布并保留FAULT；观察不授予许可，与其它Watch非原子帧 | `AppRuntime_FastISR`；`AxisStatus_Publish/LatchFault`；`BoardSafety_LatchFault` |

FOC阶段接口`OpenFOC_GetRunPhase`仅在初始化/TIM1同一所有者上下文调用，两私有字段的读取不是跨任务快照。先判无command为IDLE，再按对齐计数派生ALIGNING/CLOSED_LOOP；`foc_aligning`只是兼容镜像，定义、Init、无命令慢拍为0，每次Apply刷新。阶段不证明输出已使能或电机转动，FAULT仍由板级锁存；最终门禁撤销先关输出、FOC复位仍在临界区外完成。详见[006](specs/006-axis-run-phase.md)。

遥测保持两列三位小数CSV及CRLF；按binary32位模式做有界整数转换，最近值/中点取偶，保留负零，不调用printf或分配。NaN/Inf、编码越界或容量不足返回0并保留目标缓冲，调用端计丢帧；编码范围不代表电机运行范围，不保证所有MicroLIB舍入模式逐字节等价。`telemetry_format.c`必须加入两个Keil目标的源清单，`telemetry_format.h`须在包含路径中可达。

栈扫描在最低优先级Idle中执行，显式扫描appTask而非传NULL测Idle；appTask不会删除或重建。扫描跨抢占时可能暂漏本轮更深使用，后续更新；繁忙时可能延迟，以次数/完成时戳识别陈旧。正常目标总启用，测试目标`app_stack_probe_enabled`默认1、仅用于诊断A/B；开关不改变保护或许可。`configCHECK_FOR_STACK_OVERFLOW=2`、malloc失败钩子继续有效；空闲钩子与扫描调用链静态深度40 B、Idle最大静态深度104 B，均非运行最坏栈证明，Idle栈仍512 B。

旋变的当前时间合同：

- `ad2s1210.c PulseSample:295/311–314` 在 SAMPLE 置低并 DSB 后读取 DWT/HAL tick，表示下降沿附近的软件时间；SPI 完成时间不是采样时间，尚无硬件捕获精度证明。
- `Run_1kHz:833–851` 的角度为 `raw×2π/65536`，是**旋变电气角**；代码按旋变 4 极对将差分换成机械 rpm，再作 α=0.10 滤波。`open_foc.c SetRotorFeedback:466–470` 用方向、极对比和偏置换成电机电角；实际极对数尚待硬件确认。
- 首帧没有差分速度；失败或间隔超过 2 ms 后重建速度基准。`app_runtime.c:253–259` 要求位置、速度均有效，DWT 与 HAL 年龄都不超过现有 2 ms；已经使能时失效走锁存关断，未运行时不准启动。
- 2 ms 是当前软件新鲜度门槛，**不是任何转速下的角误差保证**。三个 ADC rank1 与 ADC3 rank2 也非同一瞬间；当前缺少 ADC 物理时戳/源帧号，没有把角度外推到电流采样或 PWM 生效时刻。
- 模式 2 的 Park 使用此前命令角，随后 FOC 推进角度；对齐阶段反馈坐标与固定输出角也不同。当前不能声称已完成 `theta_i/theta_pwm` 对时或补偿，详见 [003 第5节](specs/003-c-port-architecture.md#5-共享数据审查快照与时序方案2026-11-06)。

已实现机制和相对003的取舍见[005](specs/005-command-handoff.md)。普通目标运行配置为静态未确认值，拒绝START/UPDATE；测试目标只用合成范围演练交接。缺配置不阻挡STOP。没有实际控制通信/session（CAN目前只读），不能声称跨复位防重放。正常目标仍可能剔除未使用入口；测试目标实际链接并执行Submit/Stop/GetSnapshot。真实通信请求源接入后仍需重测。普通RTOS BASEPRI不能屏蔽TIM1 IRQ0，任务发布保存/恢复PRIMASK；NMI/Fatal先关输出、不触碰邮箱且不返回。主机顺序行为测试不证明目标原子性时长；关中断和最坏IRQ耗时须板测。


## PWM、驱动使能与故障写者

以下列出应用可达入口及其 HAL 下游；未调用的通用 HAL DMA/Stop API 不算活动写者。MOE 是定时器主输出门，EN 是板级驱动使能，50% 占空比不是关门。

| 当前写者 | 寄存器 / 行为及上下文 | 依据 |
| --- | --- | --- |
| MX_GPIO_Init / GateInactive | PB10 EN 拉低；后者还配置 MODER/OTYPER/PUPDR，供全部关断路径调用 | `gpio.c:54`；`board_safety.c GateInactive:22` |
| BoardSafety_InitSafe | 撤 EN/MOE，再把六路功率 PWM 引脚设为低电平 GPIO；main 早期及应用初始化 | `board_safety.c:48–66` |
| MX_TIM1_Init → TIM_Base_SetConfig | CR1/PSC/ARR/RCR/EGR；main 配置计数器 | `tim.c:30/52/61`；HAL `stm32h7xx_hal_tim.c TIM_Base_SetConfig:6935` |
| PWM_ConfigChannel → TIM_OCn_SetConfig | CCR1/2/3 初值 0、CCR4=1250，CCER 与预装载；main | `tim.c:79/83/87/93`；HAL `HAL_TIM_PWM_ConfigChannel:4268` |
| ConfigBreakDeadTime / MspPostInit | 写 BDTR；普通目标六路 GPIO 切 AF1，随后应用再次恢复 GPIO 低；测试目标跳过该AF调用 | `tim.c:108/138`；HAL `HAL_TIMEx_ConfigBreakDeadTime:2061` |
| AppRuntime_TaskStart → PWM_Start(CH4) | 普通目标置 CC4E、MOE、CEN，随后显式撤 MOE；测试目标只直接置 CC4E；appTask | `app_runtime.c:186/191`；HAL `HAL_TIM_PWM_Start:1463` |
| PreparePowerPWM → PWM_Start/PWMN_Start | 仅普通目标：置六路 CCER、MOE、CEN，再撤 MOE、接 AF；准备期间 EN 保持禁止 | `app_runtime.c PreparePowerPWM:65–91`；HAL `HAL_TIMEx_PWMN_Start:1112` |
| TaskStart → Base_Start_IT(TIM1) | 开 update IRQ / CEN；appTask | `app_runtime.c:194`；HAL `HAL_TIM_Base_Start_IT:462` |
| FOC_WriteDuty | 写 CCR1/2/3；main 的 Init 写初始 50%，运行时 Run_20kHz 写调制或无效/停机 50% | `open_foc.c FOC_WriteDuty:231/250–252`、`OpenFOC_Init:430/Run_20kHz:674` |
| BoardSafety_SetOutputsEnabled | 普通目标允许时置 MOE、PB10 高；唯一正常调用点在 TIM1；不允许则 DisableOutputs；测试目标恒走关断 | `board_safety.c:69–77`；`app_runtime.c:335` |
| BoardSafety_DisableOutputs | 先撤 EN、TIM1 有时钟时清 MOE，标志归 0；disabled另清六路功率CCER；main/任务/ISR/故障均可调用，保留 CH4/CEN | `board_safety.c:36–45` |
| BoardSafety_LatchFault / Fatal | 先撤输出再清 CH1–3 六路 CCER、锁存；Fatal 另禁 IRQ 且不返回 | `board_safety.c:88–117` |

`open_foc.c OpenFOC_Stop`现在由TIM1的`OpenFOC_ApplyCommand(NULL)`在许可失去沿调用，写50%并立即复位PI/启动沿，保留未来明确START能力；硬件撤输出由命令适配/board_safety先执行。`tim.c HAL_TIM_Base_MspDeInit`仍无应用调用。CCR 预装载最后由定时器更新事件生效，不等同于 CPU 写入时刻。

测试目标隔离：disabled模式也主动拉低PB10，DisableOutputs另清功率CCER；SetOutputsEnabled编译后只走关断。ISR入口只读守卫在例行关断前检查MOE/CCER/EN/六路GPIO，异常锁OUTPUT_GUARD并关断；任务复查。当前MX_TIM1_Init调用点被隔离，HAL_TIM_MspPostInit函数本体仍保留普通目标的AF代码。寄存器保证不等于物理驱动验证。

| 清除 / 恢复对象 | 当前规则与位置 |
| --- | --- |
| 板级 fault / first_fault / ipsr | `board_safety.c:15–17/88` 仅 C 初始化和运行置位；`InitSafe` 不清 fault，无运行清错 API，无应用 `NVIC_SystemReset` 调用 |
| axis_status FAULT | `axis_status.c:3/12` 与 `AXIS_STATUS_KEEP_FAULT` 保持锁存；仅复位初始化回 BOOT，无清错 API |
| AD2S 软件诊断 | `ad2s1210.c Init:507/568/730/758–759` 初始化及启动恢复清诊断；`UpdateFeedbackHealth:335` 每帧覆盖当前值、OR 累计值，均不清 board_fault |
| AD2S 芯片 fault | `ReadAndClearFault:446/458–462` 在初始化/恢复路径 SAMPLE、读 0xFF、再次 SAMPLE；非周期清错，实际器件清除效果未板测 |
| AD2S reset / ADC 事件 / 算法 reset | 0xF0 仅常量、无软件复位写入且 RESET 未接 MCU；`current_sense.c:27–29/58–60` 清 JEOS/JEOC 是事件消费；`open_foc.c:136/144/150` 是 PI/估计器复位，均不授予输出许可 |

故障入口包括 `main.c Error_Handler/assert_failed`、`freertos.c` 栈溢出/malloc/创建失败钩子、`FreeRTOSConfig.h configASSERT`、`stm32h7xx_it.c` CPU 异常和 `AppRuntime_*` 检查。反馈/ADC 运行失效走 `LatchFault` 后返回，保留诊断运行；初始化/内核/HAL 致命错误及 CPU 异常走不返回的 `Fatal`。Break/Break2/AOE 当前关闭，不能宣称已有独立硬件关断保护。

## 时钟、外设与调度

| 项目 | 当前配置 / 约束 | 依据 |
| --- | --- | --- |
| 时钟 / 电压 / Flash 等待 | SYSCLK/HCLK 200 MHz，APB 分频 2；**VOS1**；生成代码 `FLASH_LATENCY_2`（读等待；Flash编程延迟另核） | `FOC_T.ioc RCC.CortexFreq_Value/HCLKFreq_Value/PWR_Regulator_Voltage_Scale`；`main.c SystemClock_Config:150/163–189`；这些是配置事实，非电气裕量验收 |
| TIM1 | 输入时钟 200 MHz，PSC=1、ARR=2499、中心对齐、RCR=1；推算 49.98 μs / 约 20.008 kHz；代码算法保留 50 μs 常量 | `tim.c MX_TIM1_Init:46–50`；`app_runtime.c AppRuntime_Init:169`；真实频率/相位待测 |
| HAL / RTOS 时基 | HAL TIM7，RTOS SysTick 1000 Hz；两者起点不同，时间戳不可混算 | `stm32h7xx_hal_timebase_tim.c HAL_InitTick:41`；`FreeRTOSConfig.h configTICK_RATE_HZ`；`main.c HAL_TIM_PeriodElapsedCallback` |
| IRQ | 抢占 TIM1=0、TIM7=4、USART1=5、SysTick/PendSV=15，子优先级均 0；RTOS 系统调用门槛 5，TIM1/TIM7 禁调用 RTOS API | `FOC_T.ioc NVIC.*`；`FreeRTOSConfig.h configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` |
| RTOS | CMSIS_V2，appTask/StartAppTask/Normal，栈 1024 words=4096 B；heap_4，16384 B，溢出检查2、malloc hook及Idle Hook开 | `FOC_T.ioc FREERTOS.*`；`freertos.c appTask_attributes:54`；`FreeRTOSConfig.h` |
| ADC | TIM1 TRGO2 上升沿注入；U ADC1 rank1 CH16，V ADC2 rank1 CH19，W ADC3 rank1 CH10，母线 ADC3 rank2 CH1 | `adc.c MX_ADC1/2/3_Init:98–111/161–174/224–246`；禁止沿用 ADC1/JDR2 读 W 的旧假设 |
| AD2S SPI / GPIO | SPI1 Mode 1、8 bit、250 kHz，PB3/4/5；SAMPLE PB8、A1 PB9、A0 PE0、WR PE1；控制/采样来自 appTask | `spi.c MX_SPI1_Init:32/HAL_SPI_MspInit:74`；`main.h:62–69`；`ad2s1210.c` |
| FDCAN1 | HSE25 MHz，500 kbit/s，PD0 RX/PD1 TX AF9，FIFO0=8、TX FIFO=32、8B，644 B专用RAM；无IRQ/DMA | `fdcan.c MX_FDCAN1_Init/HAL_FDCAN_MspInit`；`can_link.c`只Bench Start/轮询 |
| USART / EN | USART1 115200 8N1，现有 TX_IT；PB10 EN 低为禁止、高为使能 | `usart.c MX_USART1_UART_Init:31`；`board_config.h`、`board_safety.c`；物理电平与驱动响应待测 |

## 构建与内存

| 项目 | 当前事实与依据 |
| --- | --- |
| 工具链 / 语言 | ARMCC 5.06 update 7 build 960，C99 + GNU 扩展；每个目标工程61个`.c`、1个`.s`、0 C++；不从扩展名或 AC6 遗留选项推断语言标准 |
| ABI / 优化 | 实际对象参数 `--cpu Cortex-M7.fp.dp --c99 --gnu -O0 --debug --split_sections`；VFP 硬浮点参数 ABI，MicroLIB；依据 uvprojx、对象属性与 003 记录 |
| Flash | `0x08000000`、128 KiB；普通/测试目标实际ROM分别登记在本轮验证记录 |
| RAM | DTCM `0x20000000`、128 KiB，当前使用 0 B；AXI SRAM `0x24000000`、512 KiB，两个目标RW+ZI分别见本轮验证记录；依据实际 `FOC_T.sct/map` |
| RTOS heap / appTask | `ucHeap`仍为16384 B，实际地址见本轮005 map记录；appTask 4096 B 栈运行时从此堆分配，不能再计一次静态 RAM |
| 中断栈 / 内核任务 | MSP仍4096 B，实际地址见本轮005 map记录；Idle/Timer 静态栈 512/1024 B、TCB 各 92 B；启动 C 库堆配置 512 B 但已被链接剔除 |

项目依据 [FOC_T.uvprojx](../MDK-ARM/FOC_T.uvprojx) `uAC6=0/useUlib/uC99/uGnu`；scatter 由 uVision 生成（`useFile=0`），实际 `.lnp` 引用 `MDK-ARM/FOC_T/FOC_T.sct`。当前完整版本指纹与map资源见[008构建记录](validation/008-can-query-20261116.txt)，005原map摘录留作历史；可复现全量/增量及独立策略检查命令见 [003 第4节](specs/003-c-port-architecture.md#4-构建语言标准与内存审查快照2026-11-06)。静态容量足够不等于最坏运行栈已经安全。

## DMA 与 cache：当前现状

- **没有业务 DMA。** Core 无 DMA handle、初始化、`__HAL_LINKDMA` 或启动调用；`.ioc` 没有 DMA IP/请求。`NVIC.ForceEnableDMAVector=true`、HAL DMA/MDMA 模块和驱动文件存在，只说明库可用。
- FDCAN1使用`0x4000AC00..0x4000AE84`（结束不含）的专用Message RAM，不是DMA1/2缓冲区；CPU按Device外设地址访问，工作帧在appTask栈，不新增数据缓存维护或D2 SRAM池。
- ADC 直接读 JDR；`ad2s1210.c ReadNormalHAL:490` 用阻塞 `HAL_SPI_TransmitReceive`，超时参数 10 ms；`app_runtime.c SendTelemetry:143` 用 `HAL_UART_Transmit_IT`。当前缓冲是 CPU/IRQ 共享，不是假想的 DMA 所有权链。
- Core/启动路径没有 `SCB_EnableDCache/ICache`、CCR cache 开关写入或 cache 维护。正常复位后应用没有开启 cache；调试器接管场景的实际 `SCB->CCR` 尚待板读，不能仅凭源码报告运行值。
- `main.c MPU_Config:219–242` 只有 4 GiB NO_ACCESS、非缓存、子区域禁用 `0x87` 的区域，再开 privileged default；SRAM 地址落在被禁用的子区域，**这不等于已经把所有 SRAM 配成非缓存 DMA 池**。
- 当前 scatter 无 D2 专区；`system_stm32h7xx.c:86` 的 `DATA_IN_D2_SRAM` 未定义，对应 `SystemInit` D2 SRAM 时钟分支未启用。D2 SRAM3 专用池、可达性/cache 与启停所有权仅为 [003 第9节方案](specs/003-c-port-architecture.md#9-后续-dma内存与-d-cache-合同2026-11-06规划未实施)，未落地，不据此宣称 DMA 已可用。

## 尚未闭合的边界

整组命令交接和两项时序修复已提交为1d3f101，005有有限截图支持；006只读FOC阶段接口及待机镜像已实现/编译/HOST通过，四项禁输出待机/演练观察已通过（用户截图，不含运行时长和板端版本回读）；007真实CAN RX/PING/PONG已在用户低速台架得到01/02回显及四项异常计数全0的有限证据，尚无板端版本回读、精确丢包统计、满负载或错误注入验收；008已新增四个只读查询及20 B诊断复制，源码/HOST/BUILD通过，用户STATUS与四项计数已支持有限硬件观察，非固定周期/满负载验收；电机通信命令、完整Axis生命周期、ADC/角度/PWM对时尚缺实现；最高机械转速、实际电机/旋变极对数、允许电角误差尚未确认。当前输出准备毛刺、故障关断延迟、Break 硬件路径、最坏 ISR/临界区/MSP/任务栈，以及带输出闭环均须独立验收。

继续工作以 [当前入口](README.md)、对应 spec 和版本化 validation 为准。新增方案先写 spec；实现并验证后才更新本页的“当前事实”，旧截图和旧构建记录原位保留。
