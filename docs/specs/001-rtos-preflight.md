# 001：RTOS 引入前审查与 CubeMX 配置

- 状态：**配置复查通过，可进入隔离副本生成**。尚未生成、合并或编译 RTOS 固件；最新结果见第 11 节。
- 创建日期：2026-10-29；最近复查：2026-11-04（Asia/Shanghai）。
- 工程：D:/stm32H750/High_fengshan/FOC_T；分支：Xiao_ODrive。
- 审查提交：fd025767ccac1e9b3f7ff10d5b0c36398511bbc1；原始固件基线：48c42eb / v1.0.0。
- 2026-10-29 初审开始时工作区和暂存区干净。2026-11-04 复查 HEAD=69bbebe，用户已修改 FOC_T.ioc，暂存区为空；用户负责 CubeMX 操作、验收、提交、推送、PR 和合并。
- 本规格对应用户提出的第一步：引入 RTOS 前先审查定时器、中断、HAL 时基、内存，提供可操作的配置表。
- 用户决定：2026-11-03 明确 RTOS/优先级及新的 ADC 分配；2026-11-04 要求复查配置、保存讲解偏好，并取消默认的逐项批准等待。本次执行范围是复查与文档记录，不自动执行生成或固件修改。

## 1. 文档收录与共同审查意见

用户原稿已原样收录为 [ODrive 移植约束草稿](../reference/odrive-porting-guidelines-draft.md)，来源 D:/Documents/Downloads/AGENTS.md。

原稿和工程副本 SHA256 均为 D1F42323FB045589C1B3FF2D9C7785A78694060701C51D1565F5529363B67728。原稿正文不作静默修改；它自称“执行约束”的表述不表示本次已经整体批准。根 AGENTS.md 保留团队原有规则，并增加审查入口。

| 讨论项 | 审查意见 / 建议 | 状态 |
| --- | --- | --- |
| R1：规则效力 | 原稿作为参考；先维护 spec，在用户授权范围内推进；用户明确提出需要批准时设置审批关口 | 2026-11-04 用户已更新协作偏好，见根 AGENTS.md |
| R2：阶段划分 | 本轮是 P0 中与 RTOS 有关的审查；下一步建议 P1a“RTOS 最小接入”。CAN、以太网、RS485、Flash 和完整 ODrive 状态机分后续功能处理 | 待审阅 |
| R3：文档数量 | 现状、配置、验证、遗留项先集中维护本 spec；出现跨功能复用需求时再提取 ARCHITECTURE / VALIDATION，避免同一事实多处维护 | 待审阅 |
| R4：DATA-03 | 建议解释为“使用一致副本，或有明确所有权和有效期的稳定只读数据”；无需为了形式在每个控制周期复制大结构 | 待审阅，原稿未改 |
| R5：背景与事实 | 540 V、10 kW、LAN8742A、W25Q32 等是草稿背景，尚无当前硬件连接/实测证据；不据此启用外设或设置保护阈值 | UNKNOWN |
| R6：ODrive 版本 | 官方 fw-v0.5.6 对应 a308314ed2ca613164b81e7bbdfacc53cd1859ff，作为 v3.x 开源固件参考；不是 Pro/S1/Micro 固件移植保证 | 来源已核对；本轮未迁入 ODrive 代码 |
| R7：现有保护差距 | 当前旋变故障仅诊断；Error_Handler / HardFault 等路径缺少明确的功率撤销动作。RTOS 运行成功不能代表草稿保护要求已满足 | 见第 7、9 节 |

## 2. 本轮范围与后续实施边界

2026-10-29 初审交付：根 AGENTS.md 的文档入口、本规格、用户原稿副本、基线构建记录。当次未修改固件源码、.ioc、Keil 配置和控制参数；2026-11-04 更新见第 11 节。

推荐下一步采用 FreeRTOS + CMSIS-RTOS v2：保留 TIM1 控制中断和其中的速度环，把原主循环后台工作放进一个业务任务，独立 HAL 时基，补齐任务创建失败和数据交接处理。先完成这一项可验收变化，再引入 ODrive 模块。

控制算法、PWM/ADC 时序优化、通信协议、DMA/Cache 优化、编译器升级不与首次 RTOS 接入混在一起。发现的既存问题记录下来；影响首次接入的启动、异常入口和共享数据问题在确认方案后处理。

## 3. 已核实的工具链和构建

| 项目 | 当前工程事实 | 证据 |
| --- | --- | --- |
| MCU / target | STM32H750VBTx / FOC_T | MDK-ARM/FOC_T.uvprojx:10、17 |
| 编译器 | ARMCC 5.06 update 7 build 960，uAC6=0 | uvprojx:13；本次构建输出 |
| 设备包 | Keil.STM32H7xx_DFP 4.1.3 | uvprojx、build_log.htm |
| Cube 配置版本 | .ioc 记录 CubeMX 6.16.0 / STM32Cube FW_H7 V1.12.1 | FOC_T.ioc:41、85；不等于本轮已启动 GUI 验证 |
| HAL | 1.11.5 | Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal.c:52 |
| CMSIS Core | 工程内头文件为 5.3；当前 main.d 解析到工程内 Drivers/CMSIS/Include | cmsis_version.h:35；MDK-ARM/FOC_T/main.d |
| CMSIS 元数据差异 | RTE 元数据仍写 CMSIS 4.5 / Core 4.3，构建列出外部 CMSIS 5.9 路径；不能据路径名认定实际包含的 Core 版本 | uvprojx:609；main.__i / main.d |
| 可用 RTOS 包 | 本机 H7 V1.12.1 已带 FreeRTOS V10.3.1 与 CMSIS_RTOS_V2，无需另找内核混搭 | 本机 STM32Cube Repository / Middlewares/Third_Party/FreeRTOS/Source |

2026-10-29 已执行 Keil **全量重建**，耗时 2 分 49 秒，0 Error(s)、0 Warning(s)。Code=45434、RO=754、RW=456、ZI=1784 字节。链接器报告 ROM 总量 46284 字节，约 45.20 KiB；这不是 .axf 文件的磁盘大小。

完整命令、构建输出、最新 map 摘要和产物 SHA256 保存在 [基线构建记录](../validation/001-rtos-baseline-build.txt)。本次构建后受控固件和工程配置仍无差异。编译通过只适用于当前裸机基线；RTOS 构建和上板验证尚未执行。

## 4. 当前执行路径、占用与问题

### 4.1 定时器、任务与中断现状

| 资源 / 上下文 | 当前用途 | 证据 / 首次 RTOS 接入处理 |
| --- | --- | --- |
| 启动 main | HAL/时钟/外设初始化，ADC 校准、AD2S 初始化，然后启动 ADC 注入组、TIM1_CH4 和 TIM1 更新中断 | main.c:188、227、243；保留初始化依赖 |
| TIM1 CH1/2/3 + 互补 | 三相功率 PWM | tim.c:44；保留 |
| TIM1 CH4 / TRGO2 | 内部 PWM2 的 OC4REF 上升沿触发 ADC1/2/3 注入组 | tim.c:58、83；不能删除 CH4 |
| TIM1_UP IRQ，优先级 0 | CurrentMonitor、OpenFOC 电流控制；每 20 次调用 OpenFOC_Ramp_1ms 做速度环；统计 ISR 耗时 | main.c:415；继续在 ISR，禁止 RTOS API |
| AD2S1210_Run_20kHz | 函数当前为空；ISR 使用最近一次后台采集值 | ad2s1210.c:736；不能误认为旋变采集已在 20 kHz 执行 |
| 主循环约 1 ms | AD2S1210_Run_1kHz，SPI 轮询采集位置、计算速度并发布多个变量 | main.c:264；ad2s1210.c:675；迁入业务任务 |
| 主循环持续执行 | 等待电流零偏完成、连接 PWM 脚，按 run_request 启停 EN | main.c:270；迁入业务任务并核对启停交接 |
| 主循环约 5 ms | 格式化两项速度遥测，UART 非阻塞发送 | main.c:309；同一业务任务负责，保持发送缓冲区单一所有者 |
| USART1 IRQ，优先级 5 | HAL_UART_IRQHandler，处理 IT 发送 | usart.c:62；it.c:212；保留 |
| SysTick，优先级 15 | HAL_IncTick，HAL 毫秒时基 | it.c:184；改作 RTOS tick，HAL 移至 TIM7 |
| TIM6 / TIM7 | 应用中未初始化或使用 | 全工程源码查找；选择独立向量 TIM7，TIM6 与 DAC 共享向量 |
| ADC / SPI IRQ、DMA | 当前应用未启用；ADC 读 JEOS/JDR，SPI 轮询，UART 用 IT | current_sense.c:49；本阶段保持 |
| DWT CYCCNT | 微秒延时、ISR/采集耗时统计 | ad2s1210.c:130；保留，不占 TIM6/7 |

H750 的 TIM1_UP_IRQn 是独立向量，不套用其他 STM32 的 TIM1_UP_TIM10 名称。

### 4.2 频率和等待的核算

- HSE 配置 25 MHz；PLL M=2/N=32/P=2，CPU/HCLK=200 MHz；四个 APB 为 100 MHz，当前 APB1/2 定时器时钟为 200 MHz。
- TIM1：PSC=1，计数时钟 100 MHz；中心对齐 ARR=2499，按 RM0433 的完整往返 2×ARR 计数，稳态 PWM 约 20008.003 Hz，周期 49.98 us。RCR=1 后更新 IRQ 同频；速度环每 20 次约 1000.4 Hz。代码中的“20 kHz / 1 ms”是标称值。
- 本阶段保留 ARR=2499、RCR=1、CCR4=1250；严格 20 kHz 校正及所有 dt 依赖另议，不能为加 RTOS 顺手改参数。RCR 若被生成器改回 0，控制更新可能接近 40 kHz。
- SPI1 为 64 MHz / 256 = 250 kHz；一帧 24 bit 的线上时间至少 96 us，另有 GPIO 延时和软件开销。它应留在后台任务，不能放进约 50 us 的 TIM1 控制 ISR。
- 当前正常采集调用 HAL_SPI_TransmitReceive，timeout=10 ms；失败路径可能跨多个 1 ms 周期。位置差分测速却使用固定 1000 Hz 系数（ad2s1210.c:26、103）。需要记录真实采样周期、失败后的间隔和超期，不能把 osDelayUntil 当作硬实时保证。
- 当前 Core 正常路径未发现 PRIMASK 临界区；Error_Handler 使用 __disable_irq。新增 RTOS/共享数据后重新审查关中断区及耗时。

### 4.3 共享数据交接清单

| 数据 | 写入者 → 读取者 | 当前缺口 / 下一步要求 |
| --- | --- | --- |
| 旋变角度 rad、机械速度 rpm、valid、采样序号/时间 | 当前 main / 未来 appTask → TIM1 ISR | ad2s1210.c:713 逐字段写，main.c:429 逐字段读；可能混帧，volatile 不构成整帧保证 |
| run_request、控制模式/目标 | 调试器及应用入口 → TIM1 / 后台启停逻辑 | 初版维持单一业务入口；后续通信不可任意并发写；启用 EN 前必须复核许可 |
| power_stage_enabled | 主循环及 TIM1 → 双方 | main.c:291 与 :458 存在交接；需要防止停止请求到达后被后台重新使能 |
| 电流/母线与零偏 ready | TIM1 中 CurrentMonitor → 控制算法 / 后台启动判断 | ADC 一组数据来自 JEOS/JDR；ready 与初始化顺序需保持 |
| 两项速度遥测 | TIM1 → appTask → UART IRQ 读取静态发送缓冲区 | 两值要形成一致快照；busy 时丢弃本次遥测，不复用在发缓冲区 |

阈值选 5 时，taskENTER_CRITICAL 使用的 BASEPRI 不屏蔽优先级 0 的 TIM1；不能拿它充当上述数据保护。实现前需在本规格补齐最终结构、发布 API 和时序预算。可评审的简单方向是：在临界区外计算，保存/恢复 PRIMASK 的短固定长度区间只负责发布；不包含 SPI、浮点运算或格式化，并实测新增中断延迟。也可使用经证明的所有权方案；仅说“双缓冲+volatile 索引”仍不足以证明生命周期和顺序。

## 5. CubeMX 配置顺序：先在隔离副本生成

2026-10-29 初审时 .ioc 缺少的外设与 TIM1 参数现已补齐。2026-11-04 复查通过，允许进入隔离副本生成；原工程手写代码尚未同步新的 ADC 分配及 RTOS。

1. 保存最新 .ioc；按用户的手动 Git 习惯，可先把当前配置和文档提交为生成前检查点。无需另设默认批准关口。
2. 用 CubeMX 打开正式工程的 FOC_T.ioc，保持 MCU、H7 V1.12.1 包和现有 200 MHz 时钟树。遇到升级提示先保持当前版本。
3. 第 6 节配置已由用户补齐并通过静态复查；在工程副本打开同名 .ioc，确认 MCU、包版本和目标生成目录。
4. 在 Project Manager 保留 MDK-ARM 工程输出、现有工程名/目录、每个外设独立 .c/.h 与 Keep User Code；建议打开生成前备份、关闭 Delete previously generated files when not re-generated。
5. **在隔离副本点击 Generate Code，避免直接覆盖正式工程。** Keep User Code 不能保护生成区的手工逻辑。
6. 生成后先由 AI 审查差异、重复中断/回调和手写初始化保留情况，再按后续任务范围逐文件合并。不得整目录覆盖 Core 或 MDK-ARM。

当前分支仍为 Xiao_ODrive。Git 提交、PR 与分支整理由用户执行；若 demo PR 尚未合并，建议先完成它，再从同步后的 main 建立本功能分支，避免新功能混入 demo PR。

## 6. RTOS 与外设配置表

以下“恢复”是把**现有源码行为**补回 .ioc，不是新设计已通过硬件验证；“新增”是待确认的 RTOS 配置。CubeMX 不同版本字段位置可能略有差异，生成结果还需复查。

### 6.1 时基、中断与内核

| CubeMX 区域 / 参数 | 建议填写 / 保持 | 类型与理由 |
| --- | --- | --- |
| System Core → SYS → Timebase Source | TIM7 | 新增 HAL 专用时基；不要另建一套会重复初始化的 MX_TIM7 业务定时器 |
| TIM7 / HAL tick | 1000 Hz；200 MHz timer 时钟下可对应 PSC=199、ARR=999，以生成 HAL_InitTick 计算为准 | HAL_Init 与 SystemClock_Config 后重配均应正确；启动阶段也必须走时 |
| NVIC Priority Group | 4 bits for pre-emption / 0 bits for subpriority，即 NVIC_PRIORITYGROUP_4 | 保持；本 MCU __NVIC_PRIO_BITS=4 |
| TIM1 update IRQ | Enabled，抢占 0，子优先级 0；Uses FreeRTOS functions 不选 | 恢复；保留高频控制路径，禁止全部内核 API |
| TIM7 IRQ / TICK_INT_PRIORITY | 抢占 4，子优先级 0；TICK_INT_PRIORITY=4；不使用 RTOS API | 新增方案：HAL tick 不受阈值 5 的 BASEPRI 临界区屏蔽；仍低于 TIM1。NVIC 与 HAL_InitTick 的实参要一致，不能只改其中一处 |
| USART1 global IRQ | Enabled，抢占 5，子优先级 0 | 恢复；当前不调用内核，将来只用适用的 ISR API |
| SysTick / PendSV | 抢占 15，子优先级 0 | RTOS 内核使用；SVC 按端口生成，不能保留旧的空 handler |
| Middleware → FREERTOS Interface | CMSIS_V2 | 新增；同包的 CMSIS-RTOS v2 wrapper + FreeRTOS V10.3.1 |
| configTICK_RATE_HZ | 1000 Hz | 1 tick = 1 ms；与 HAL tick 是两个计数来源 |
| configUSE_PREEMPTION / configUSE_TICKLESS_IDLE | 1 / 0 | 首版抢占调度、关闭 tickless |
| configLIBRARY_LOWEST_INTERRUPT_PRIORITY | 15 | 对应内核编码 0xF0 |
| configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY | 5 | 本项目建议门槛；移位后 configMAX_SYSCALL_INTERRUPT_PRIORITY=0x50，不能填进 HAL 的未移位字段 |
| CMSIS v2 依赖 | 保留兼容默认：configMAX_PRIORITIES=56、PORT_OPTIMISED_TASK_SELECTION=0、32 bit tick；保留软件定时器支持 | 本地 freertos_os2.h 有硬性检查；不要为了精简改成常见原生 FreeRTOS 的 5 个优先级 |
| 软件 timer 任务 | 首版可用 priority=2、queue length=10、stack depth=256 words | 原生任务优先级/栈单位，生成后核对；不是 NVIC 数值 2 |
| 应用任务 | 一个 appTask（可重命名 defaultTask），入口 StartAppTask，osPriorityNormal | 首版承接原主循环后台职责；暂不新建 20 kHz FOC task |
| appTask 栈 | 4096 字节；若界面标 words 则填 1024 | 以生成的 osThreadAttr_t.stack_size=4096 为准，包含浮点 snprintf 的初始预算 |
| 应用软件 timer / queue / mutex | 本阶段不额外创建 | 内核 timer 任务的存在不代表要用软件定时器跑控制环 |

NVIC 数字越小抢占优先级越高；0–4 的 IRQ 不得调用任何 RTOS API，包括 FromISR。5–15 也只允许对应封装支持的 ISR API，不能阻塞。任务优先级的高低规则和 NVIC 数字不同。

### 6.2 TIM1 与引脚：按当前代码恢复

| 参数 | CubeMX 目标配置 |
| --- | --- |
| 时钟 | Internal Clock，timer kernel=200 MHz，Prescaler=1 |
| 计数 | Center Aligned Mode 1；Counter Period=2499；Repetition Counter=1；Clock Division=DIV1；Auto-reload preload=Disable |
| CH1/CH2/CH3 | PWM Generation CHx CHxN；PWM mode 1；初始 Pulse=0；主/互补极性 High；Idle State Reset；Fast Mode Disable |
| CH4 | 内部通道 / 无引脚输出的 PWM mode 2，Pulse=1250；Cube 如显示 PWM No Output 使用该方式，生成后确认 OC4REF 路径；不要占用现有串口或其他引脚 |
| Trigger Output / Trigger Output 2 | Update Event / OC4REF；Master/Slave mode Disable |
| Break and Dead Time | Dead Time=100（当前时钟推算约 500 ns）；OSSR/OSSI Disable；Lock Off；Break/Break2 Disable；Automatic Output Disable；保留原有极性 High、filter 0 |
| PWM 脚 | PA8=CH1，PA9=CH2，PA10=CH3；PE8=CH1N，PE10=CH2N，PB1=CH3N；AF1、NoPull、Low Speed |

Break 当前关闭是现状，不能据此宣称存在 TIM1 硬件过流关断。是否增加 BKIN 要结合真实硬件路径另审。这里的参数也不代替死区与波形实测。

### 6.3 ADC、SPI、UART 与 GPIO：补齐 .ioc 缺项

| 项目 | CubeMX 目标配置 / 注意项 | 现源码证据 |
| --- | --- | --- |
| RCC per_ck / ADC / SPI123 | per_ck=HSI 64 MHz；ADC kernel 选 CLKP/per_ck；SPI123 同样选 CLKP/per_ck；保留 HSE 主系统时钟 | adc.c:106；spi.c:88 |
| ADC1/2/3 共用参数 | 12-bit，asynchronous clock /4，即 ADC 时钟 16 MHz；single-ended；scan enable；continuous/discontinuous/auto-wait/oversampling off；DR 数据管理，Overrun preserved，Left shift none，EOC end of sequence | adc.c:7 |
| ADC regular group | 保留 1 conversion、Software start、无触发边沿；现应用不启动 regular group；不要把实际注入采样迁到 regular | adc.c:15；current_sense.c:32 |
| ADC injected 共用参数 | TIM1 TRGO2 rising edge；Sampling Time 64.5 cycles；AutoInjected/Discontinuous/QueueContext/Oversampling Disable；Offset none | adc.c:25 |
| ADC1 | Independent；Injected 1 rank：rank1 INP16 / PA0 / U 相 | 2026-11-03 用户最终方案；FOC_T.ioc:8；旧 adc.c 尚待合并 |
| ADC2 | Injected 1 rank：INP19 / PA5 / V 相 | adc.c:77 |
| ADC3 | Injected 2 ranks：rank1 INP10 / PC0 / W 相；rank2 INP1 / **PC3_C** / 母线；保留 SYSCFG_SWITCH_PC3_OPEN | 2026-11-03 用户最终方案；FOC_T.ioc:41；旧 adc.c 尚待合并 |
| ADC 模拟脚 | 对可配置的 ADC GPIO 设置 Analog / NoPull；PC3_C 专用模拟焊盘按芯片映射及上述模拟开关处理 | adc.c:123、149 |
| ADC IRQ / DMA | 不额外使能；注入完成由 TIM1 路径检查三路 JEOS 后读 JDR | current_sense.c:49 |
| SPI1 基本配置 | Master / Full Duplex；8 bit；MSB first；CPOL Low、CPHA 2 Edge（Mode 1）；Software NSS；prescaler 256 → 250 kHz | spi.c:40 |
| SPI1 其他参数 | NSS pulse Disable；TI/CRC Disable；FIFO threshold 1 data；NSS polarity Low；SS/inter-data idleness 0；receiver auto-suspend、keep IO state、IO swap 均 Disable | spi.c:52 |
| SPI1 脚 / IRQ / DMA | PB3 SCK、PB4 MISO、PB5 MOSI，AF5/NoPull/Low Speed；无 IRQ/DMA | spi.c:100 |
| USART1 | Asynchronous TX/RX；PB6 TX / PB7 RX，AF7/NoPull/Low Speed；115200、8N1、无流控、oversampling16、prescaler1；FIFO Disable；clock D2PCLK2；IRQ=5 | usart.c:6 |
| PB10 功率 EN | GPIO Output Push Pull / NoPull / Low Speed / 初始 Low；高电平有效 | board_config.h:18；main.c:83 |
| AD2S 控制 GPIO | PE1=WR、PE0=A0、PB9=A1、PB8=SAMPLE；Output Push Pull / NoPull / Low Speed / 初始 High | ad2s1210.c:38、386 |
| 调试 | Serial Wire；保留 PA13/PA14；不要打开抢占 PB3 的 SWO 复用 | .ioc；SPI1 SCK 占 PB3 |
| CAN / ETH / LwIP / RS485 DE / QSPI | 本阶段不新增；USART1 当前遥测不能直接认定为已实现 RS485 协议 | 后续各自建规格 |

引脚表依据现源码，不替代硬件连接确认。如果 CubeMX 报引脚冲突、通道不可选或 ADC 参数不一致，记录具体字段并复审，不为了消除提示任意换脚。

### 6.4 内存、端口与调试检查

| 项目 | 当前值 | 首版 RTOS 建议与验证 |
| --- | --- | --- |
| Flash | 0x08000000，长度 0x20000 = 128 KiB | 保持；生成后全量链接检查，不能按非保证容量扩展 |
| DTCM / AXI RAM | 0x20000000 / 128 KiB；0x24000000 / 512 KiB | 保留目标内存布局；本次新 map 的 RW/ZI 全在 AXI，DTCM 分配为 0 |
| 链接方式 | useFile=0，Keil 自动 scatter；两个 RAM 区均有 .ANY(+RW +ZI) | 不手工修改输出目录下的自动 .sct；RTOS 堆/栈究竟在哪以新 map 为准 |
| MSP / startup Stack Size | 0x400 = 1024 字节 | Project Manager 改 0x1000 = 4096 字节；核对 startup.s 和 map，测最坏嵌套余量 |
| C 库 Heap Size | 0x200 = 512 字节 | 暂保持；它不是 FreeRTOS 堆 |
| FreeRTOS allocation | 未引入 | Dynamic allocation + heap_4，configTOTAL_HEAP_SIZE=16384 字节；仅初始化创建资源，一次只编译一种 heap 实现 |
| Static allocation 支持 | 未引入 | 保留 CMSIS v2 / Cube 生成的兼容设置；若生成静态 idle/timer 回调，核对其栈和对象；不要盲删 |
| 栈溢出 / malloc failure / assert | 未引入 | CHECK_FOR_STACK_OVERFLOW=2、USE_MALLOC_FAILED_HOOK=1，启用 configASSERT；下一步补有效 hook，不能停在空 weak 实现 |
| 任务栈与余量 | 无 RTOS 任务 | 初始 appTask 4096 B；CMSIS stack_size/stack space 是字节，原生 FreeRTOS 栈深/高水位是 StackType_t 个数，本机每项 4 B |
| CPU port | ARMCC5 / Cortex-M7 | 使用当前 H7 包的 RVDS 系列、仅一份 port；该包建议非 r0p1 的 M7 使用 ARM_CM4F，不能看到目录名就换成 GCC 或强制 r0p1 port |
| Cache / MPU / DMA | Core 未发现启用 I/D Cache；已有默认 MPU 保护；应用无 DMA buffer | 本阶段保留。MPU 的关闭子区走默认映射，不等于全部 RAM 不可缓存；上板另读 CCR 确认 |

内存预算是起始值，未宣称满足未来网络协议栈。完成后记录 xPortGetFreeHeapSize、xPortGetMinimumEverFreeHeapSize 和任务栈余量；MSP 与任务栈分别核查。普通 DMA1/2 不可访问 ITCM/DTCM，MDMA 不同；未来加入 DMA 时重新设计可达区域、32 字节缓存行边界和缓存维护，不能仅靠 volatile。

## 7. 配置之后需要实施的代码工作

1. 对照生成目录和本基线，补回全部手工外设配置与用户模块到 Keil；保留 ARMCC5、现有数学库和 HAL/CMSIS 依赖。
2. 合并 HAL 时基：只保留一份 HAL_TIM_PeriodElapsedCallback。TIM1 分支执行现有 FOC；TIM7 分支仅递增 HAL tick。不能在 TIM1 分支增 HAL 毫秒，也不能将 FOC 跑到 TIM7 分支。
3. 正确接入 SysTick/SVC/PendSV；消除旧空 handler 与 port 重复定义。USART1_IRQHandler 当前在 USER CODE 区，生成器可能另生成同名函数，需合并。
4. 将原 while 后台逻辑迁到 StartAppTask，用绝对 tick 的周期等待和超期记录；保留约 1 ms 采集与约 5 ms 遥测，避免相对延时累计漂移。错过 deadline 时统计并跳过过期周期或重置基准，不连续追赶过去的 osDelayUntil 目标造成忙循环。SPI 只由该任务正常访问；HAL_Delay 保留在调度器启动前的初始化路径，任务周期等待使用 RTOS API。
5. 审查启动顺序：PB10 先 Low，保留 BoardPowerStage_Init / BoardPWM_PinsSafeGPIO；先校准并准备 ADC，再启动内部 CH4 / TIM1。建议让业务任务启动后才开启这条采样链，避免内核对象未就绪时进入新逻辑；六路功率输出仍受零偏 ready 和运行请求控制。默认 foc_run_request=0。
6. 补齐第 4.3 节的发布协议与 EN 启停交接；最终接口、关中断时长上限和采样超龄处理需在本 spec 中明确后实施。采集失败后不得将跨多个周期的角度差直接冒充一个 1 ms 增量。
7. 检查每个对象/线程创建结果和 osKernelStart 异常返回。覆盖 malloc-failed、stack-overflow、assert 等入口，明确故障记录与功率撤销；现 Error_Handler 只有关中断死循环，定时器可能继续输出，不能用它作为已完成的保护实现。
8. 全量编译并看 map，再按第 8 节验收。用户授权具体板上测试条件后才进入硬件验证。

其中第 4、5、6、7 项是代码工作，CubeMX 勾选不能完成。尤其旋变 10 ms 超时、固定 dt 测速和完整反馈故障停机策略不能因为“任务能运行”而标通过；是否在本功能内一并修正需用户审阅确定范围。

生成前需重点保护：main.c:216、225 的安全初始化调用位于 USER CODE 外；main.c:255 的旧无限循环在 osKernelStart 后可能不可达；adc.c/usart.c 基本是手写；tim.c 的 RCR/CH4/TRGO2/死区在生成区；it.c:205 的 TIM1 handler 也不能靠旧 .ioc 保留。

## 8. 验收场景与本轮实际结果

| 验收项 | 通过标准 | 本轮状态 |
| --- | --- | --- |
| 文档导入 | 原稿与副本 SHA256 相同；原团队规则保留；争议项进入 spec | PASS：文档检查 |
| 基线构建 | 对审查提交全量编译，记录工具/命令/结果与 map | PASS：2026-10-29 裸机基线 0 错误 0 警告；不是新 RTOS 代码结果 |
| .ioc 配置复查 | 与用户最终外设/引脚、TIM1、RTOS 和 IRQ 目标一致，既有源码差异单独列出 | PASS：2026-11-04 可隔离生成；VOS 差异等见第 11 节，生成后的实现仍待核查 |
| RTOS 构建 | 内核/wrapper/port/heap 各自匹配，无重复 handler；全量编译通过；Flash/RAM 不越界 | NOT_RUN |
| 双时基 | 调度器启动前后 HAL_GetTick 均每毫秒增长；RTOS tick 1000 Hz；停一个业务任务不停止 HAL tick | NOT_RUN：需要新固件和测量 |
| 控制时序 | 对照基线测 PWM、TRGO2/ADC、ISR 周期/抖动；无新增控制超期；按实际 49.98 us 周期复核统计阈值与可用预算 | NOT_RUN：当前没有基线波形/WCET 数值 |
| 后台周期 | 记录采集完成间隔、最大耗时和 deadline miss；无连续追赶造成忙循环；失败和旧数据处理符合确认后的策略 | NOT_RUN；阈值/策略待审阅 |
| 数据交接 | 注入任务/中断交错，控制不读取半更新帧；停止请求不会被后台重新使能；ISR 不等待低优先级写者 | NOT_RUN |
| 内存 | 对象创建成功，栈/堆余量满足事先确定的裕量；故障 hook 触发行为有记录 | NOT_RUN；4096 B / 16 KiB 仅初始预算 |
| 故障与输出 | 线程创建失败、栈/堆故障、初始化失败时均按确认方案撤销许可；关断电平和路径由实板验证 | NOT_RUN / UNKNOWN：具体硬件条件待补 |

2026-10-29 初审检查 ID 的历史记录（最新 .ioc 复查见第 11 节）：BASE-01 SRC PASS；BASE-02 与 RTOS 相关工具/构建来源 SRC PASS；BASE-03 BUILD PASS；BASE-06 基线标识 SRC PASS。HW-01 的 .ioc/源码一致性 SRC FAIL；DATA-03 的整帧交接 SRC FAIL；SAFE-06 的现有异常撤销输出路径 SRC FAIL。TIME-03、MEM-04、VERIFY-04 的 RTOS/硬件证据 NOT_RUN。BASE-04 的真实电压、负载、方向和测试记录 UNKNOWN。其余原稿条目本轮未逐项审计，不统一勾选通过。

2026-10-29 修改总结：添加原稿与规格、建立根规则入口、归档全量编译证据。原稿副本哈希一致；新增文件 UTF-8 解码检查通过；Git 差异检查通过；已对定时器/中断与内存/端口配置作交叉复审。固件、.ioc、Keil 配置无差异，暂存区为空。未暂存、提交、推送、合并、生成 RTOS 固件或操作硬件。

## 9. 当前进度与下一步

- [x] 已按用户方案配置 FreeRTOS + CMSIS v2，一个 appTask；现有 FOC 保持在 TIM1 ISR，任务业务代码待迁入。
- [x] TIM7 时基/优先级 4、SysTick/PendSV 15、门槛 5、初始内存预算的配置已复查；生成后实际宏和时基代码待验证。
- [x] TIM1 的 PSC/ARR/RCR/CCR4 保持原值；约 20.008 kHz 与标称 20 kHz 的差异仍记录，未修改控制时序。
- [x] 用户已保存 .ioc，AI 完成生成前配置复查。
- [ ] 在隔离副本生成，再审查差异并按后续任务范围合并代码。
- [ ] 实施前细化数据发布、采集超期/固定 dt、启动与异常处理的具体接口和验收阈值；解释取舍并更新本规格。用户明确要求批准或确需其决策时再提出。
- [ ] 补充真实板卡版本、当前已验证供电/负载/模式、基线 ISR 最坏耗时与波形；缺少这些不妨碍当前 .ioc 审查，但不能宣称上板完成。

回退：本轮纯文档可单独撤销；后续 Cube 生成只合入已审阅差异。已共享的提交使用新 revert 提交，不改写同伴历史；v1.0.0 不移动。

## 10. 官方资料与本地核查入口

- [FreeRTOS Cortex-M 中断优先级](https://www.freertos.org/Documentation/02-Kernel/03-Supported-devices/04-Demos/ARM-Cortex/RTOS-Cortex-M3-M4)：优先级门槛与移位值；本规格 5/4/15 是本工程候选分配。
- [Arm CMSIS-RTOS2 线程管理](https://arm-software.github.io/CMSIS_5/RTOS2/html/group__CMSIS__RTOS__ThreadMgmt.html)：stack_size 字节单位。
- [ST RM0433](https://www.st.com/resource/en/reference_manual/rm0433-stm32h742-stm32h743753-and-stm32h750-value-line-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)：TIM1/TIM8 中心对齐计数、时钟和内存域。
- [STM32H750 数据手册](https://www.st.com/resource/en/datasheet/stm32h750vb.pdf)：器件保证的 Flash/RAM 与引脚资料。
- [ST H7 DMA FAQ](https://community.st.com/stm32-mcus-60/dma-is-not-working-on-stm32h7-devices-97)、[AN4839](https://www.st.com/resource/en/application_note/an4839-level-1-cache-on-stm32f7-series-and-stm32h7-series-stmicroelectronics.pdf)：DMA 可达性与缓存一致性。
- [ODrive fw-v0.5.6](https://github.com/odriverobotics/ODrive/releases/tag/fw-v0.5.6)：原稿参考版本；本轮不迁入实现。
- 本地 RTOS 包：C:/Users/LEGION/STM32Cube/Repository/STM32Cube_FW_H7_V1.12.1/Middlewares/Third_Party/FreeRTOS/Source；核查 include/task.h、CMSIS_RTOS_V2/freertos_os2.h、CMSIS_RTOS_V2/cmsis_os2.c、portable/RVDS/ARM_CM7/ReadMe.txt。

## 11. 2026-11-04 配置复查与操作说明

- 当前 HEAD：69bbebe；分支 Xiao_ODrive，比本地记录的 origin/Xiao_ODrive 超前 1 次提交。本轮未 fetch，不能据此断言 GitHub 最新状态。
- 复查开始时仅用户的 FOC_T.ioc 未暂存；本轮 AI 仅更新 AGENTS.md 与本规格，未修改 .ioc、固件或 Keil 配置，未生成/编译/烧录。
- 所检查 .ioc SHA256：6934C60A58D0EEAC89784B499DEC9557F2A0C90DC41954EC39F5B8EAE0A423E1。用户之后再保存时需按新内容复查。

| 检查项 | 当前结果 / 证据 |
| --- | --- |
| appTask | FOC_T.ioc:60，入口 StartAppTask，Normal=24，1024 words=4096 B；唯一业务任务 |
| RTOS 内存/检查 | .ioc:61–63：栈检查2、堆16384、malloc钩子1；CMSIS_V2 在358；tick1000/heap4/syscall5沿用已核查的 Cube 数据库默认 |
| 优先级与时基 | .ioc:129–140：TIM1=0、TIM7=4、USART1=5、SysTick/PendSV=15，子优先级0，GROUP4；HAL选TIM7 |
| MSP | .ioc:235 已改0x1000；这是中断/启动的栈预算，与任务自己的栈分开 |
| USART1 时钟 | .ioc:313 为100MHz，已删除CSI源显式设置，回到本机Cube数据库默认D2PCLK2；与usart.c:45一致 |
| ADC | U=ADC1 R1 CH16、V=ADC2 R1 CH19、W=ADC3 R1 CH10、母线=ADC3 R2 CH1；TIM1_TRGO2上升沿注入方案保持正确 |
| SPI / GPIO / PWM | SPI1 Mode1/250kHz/PB3-5，AD2S四GPIO初始High，PB10 EN初始Low；TIM1 CH4内部PWM2/TRGO2/RCR1保持 |
| 新增 VOS 差异 | .ioc:293为VOS1，main.c:371仍VOS3；本机Cube数据库在CPU≤200MHz下允许两档，不阻塞隔离生成，但合并前须明确最终档位并检查Flash等待周期联动 |

复查结论：上轮三个阻塞项（任务入口、TIM1/TIM7优先级）均已修正。可以进行隔离副本生成；这是配置层面的通过，不是 RTOS 已经运行或电机已通过验证。

用生活例子理解：TIM1 像生产线上必须按固定节拍工作的工位；FreeRTOS 像安排后台工作的调度员，appTask 是先配的一名后台工作人员。TIM7 和 SysTick 是两块分工不同的钟，分别给 HAL 和调度员计时。把调度员引入工厂，不应改变关键工位的节拍。

建议用户下一步：

1. 保存 CubeMX 当前配置。需要 Git 检查点时，由用户提交本次 .ioc 与文档，建议说明 docs/config: prepare RTOS generation；不要求先合并 PR 才能继续隔离实验。
2. 在工程目录之外新建一个生成专用目录，例如 D:/stm32H750/High_fengshan/FOC_T_rtos_gen_20261104，并复制当前工程工作文件。可不复制 .git 与旧编译输出；必须带上最新未提交的 .ioc。不要只 clone 旧提交而遗漏刚配置的内容。
3. 用 CubeMX 打开副本里的 FOC_T.ioc，检查 Project Location 和实际生成目标均位于副本内；工程名保持 FOC_T，包保持 H7 V1.12.1，目标工具链保持 MDK-ARM。
4. 在副本中建议开启 Backup previously generated files、关闭 Delete previously generated files when not re-generated，保留 Keep User Code；这些设置不保证保护所有手工代码。
5. 在副本点 Generate Code。随后进入生成差异审查，不把生成目录整包复制回正式工程，也不直接拿生成结果上板。

生成/合并阶段要防的具体风险：

- 原手写 W 相读取 current_sense.c:70 仍是 ADC1->JDR2，必须改 ADC3->JDR1；母线第72行改 ADC3->JDR2，旧 adc.c 序列/PC0归属一并更新。否则像换了仪表接线却还按旧标签读数。
- main.c:216、225的安全初始化位于生成区，可能被覆盖；SysTick/SVC/PendSV和HAL_TIM_PeriodElapsedCallback可能重定义或接错。需要逐处合并，不能只看编译是否通过。
- ADC1 regular CH16仍配置1.5cycles，注入CH16为64.5cycles（.ioc:2、20、13）；应用不使用regular，可关闭以减少歧义；生成后核对同一通道最终采样时间。
- VOS1是相对基线VOS3的供电档位变化，不是RTOS必需条件。若无改变档位的需求，后续配置采用基线VOS3；本轮未自动替用户修改。
- 任务/中断共享数据、失败hook、实际堆栈余量以及控制抖动仍未验证。配置正确只能说明设计图上的选项已对齐，不能证明工厂运行无误。

本轮验证：复查 .ioc、手写代码、现有 spec 与本机 Cube 默认；交叉复核优先级/ADC/USART/VOS差异。未运行新的构建或硬件测试。
