# 001：RTOS 引入前审查与 CubeMX 配置

**一句话：** 本spec保留RTOS接入前检查、接入实施及其验收；当前系统事实统一维护在[架构页](../ARCHITECTURE.md)，后续功能不再叠加到这里。

**状态：** 已实现（RTOS最小接入，1668486）。源码检查PASS、编译PASS、编译期检查PASS；主机运行测试NOT_RUN；基础隔离供电板测PASS（有限观察），完整硬件验收NOT_RUN，证据与边界见原第13–15节。日常入口：[README](../README.md)。

| 改了哪里 | 改前 → 改后 | 为什么 | 谁执行、交给谁 | 验证情况 |
| --- | --- | --- | --- | --- |
| 后台入口 | 裸机循环 → 单一appTask | 在RTOS下继续后台业务 | StartAppTask → AppRuntime_BackgroundStep | 第13节实施；第15节有限观察 |
| 时基 | 旧HAL tick配置 → TIM7/HAL、SysTick/RTOS分离 | 避免入口冲突 | TIM7/SysTick → 各自tick | 第13节源码/构建；第15节粗查 |
| 输入/安全交接 | 独立旧数据与失败处理 → 快照、有效期、故障撤输出 | 约束任务与TIM1协作 | appTask → TIM1；board_safety故障撤输出 | 真实关断/物理对时等仍未关闭 |

**阅读地图：** 第1–10节是接入前审查快照；第11–12节是配置/生成历史；第13节为接入实施；第14节为当时测试方法；第15节为1668486基础观察。正文中的“当前”均指对应日期，旧VOS/ADC/使能描述不作为现配置。当前待办在[README](../README.md)保留回链。

**历史来源说明：** 第1节原稿SHA是初次收录时的指纹；2026-11-06用户另行授权协调参考稿的文档入口，现参考稿不再声称与初次原件逐字节相同，约束ID保留，见[004](004-docs-organization.md)。

## 历史过程与证据

- 状态：**RTOS最小接入已提交为1668486；Keil编译通过，母线隔离条件下基础运行初检通过，完整实板验收未完成**。生成检查点为996b818；实现见第13节，实测证据和边界见第15节。2026-11-06起下一小步见[002运行状态观测](002-axis-status-observer.md)，本规格保留RTOS未测项，不把提交标题等同于完整验收。
- 创建日期：2026-10-29；最近复查：2026-11-04（Asia/Shanghai）。
- 工程：D:/stm32H750/High_fengshan/FOC_T；分支：Xiao_ODrive。
- 审查提交：fd025767ccac1e9b3f7ff10d5b0c36398511bbc1；原始固件基线：48c42eb / v1.0.0。
- 2026-10-29 初审开始时工作区和暂存区干净。2026-11-04 复查 HEAD=69bbebe，用户已修改 FOC_T.ioc，暂存区为空；用户负责 CubeMX 操作、验收、提交、推送、PR 和合并。
- 本规格对应用户提出的第一步：引入 RTOS 前先审查定时器、中断、HAL 时基、内存，提供可操作的配置表。
- 用户决定：2026-11-03 明确 RTOS/优先级及新的 ADC 分配；2026-11-04 更新讲解与批准偏好，随后因空间不足选择在现有工程原地生成。用户先提交 c36ed99，再完成 CubeMX 生成并请求检查。本次 AI 执行差异审查、全量构建和文档记录，没有修改固件源码或再次生成。

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

2026-10-29 的命令、构建输出、map 摘要和产物 SHA256 保存在 [基线构建记录](../validation/001-rtos-baseline-build.txt)。当时构建后受控固件和工程配置无差异，该次通过仅适用于裸机基线；2026-11-04 的 RTOS 构建失败结果见第 12 节，上板验证仍未执行。

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

## 5. CubeMX 生成策略与已执行操作

用户于 2026-11-04 因空间不足明确选择在现有工程生成，替代此前的隔离副本建议；这是当前生效策略，不要求另建工程或等待 PR 合并。

1. 用户已提交 c36ed99（前cubeMx架构）；生成前工作区干净，关键源码、FOC_T.ioc 和 Keil 工程已受 Git 跟踪。
2. 用户已用当前 H7 V1.12.1 / MDK-ARM 配置在现有 FOC_T 目录生成。FOC_T.ioc 相对检查点仅 DeletePrevious=true 改为 false；KeepUserCode=true、BackupPrevious=false 保持。
3. AI 已检查生成差异并全量构建；结果为 2 错误、2 警告，详见第 12 节。
4. 后续在当前分支逐项修补并审查差异，不整包恢复旧 Core 以免撤掉新 RTOS/ADC 配置；也不把原始生成结果视作可运行固件。
5. 每轮保留同一份规格与验证证据。Git 暂存、提交、推送、PR、合并仍由用户执行；本次没有烧录、调试或上电。

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

1. 对照原地生成后的工作区与 c36ed99 检查点，修补生成区覆盖的手写逻辑；保留 ARMCC5、数学库和 HAL/CMSIS 依赖。已确认原 39 个 Keil 源码条目全部保留，RTOS 等新增 12 个条目均存在；不需重复添加已有模块。
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
| .ioc 配置复查 | 与用户最终外设/引脚、TIM1、RTOS 和 IRQ 目标一致，既有源码差异单独列出 | PASS：2026-11-04 生成配置与主要外设实现已核对；应用代码差异和 VOS 变更见第 12 节 |
| RTOS 构建 | 内核/wrapper/port/heap 各自匹配，无重复 handler；全量编译通过；Flash/RAM 不越界 | PASS：第 13 节接入后全量编译0错误0警告；ROM59576 B、RW+ZI25336 B，未越界。第 12 节保留原始生成失败历史 |
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
- [x] TIM7 时基/优先级 4、SysTick/PendSV 15、门槛 5、初始内存预算的配置与生成宏/时基实现已静态核对；实际走时和余量仍待运行验证。
- [x] TIM1 的 PSC/ARR/RCR/CCR4 保持原值；约 20.008 kHz 与标称 20 kHz 的差异仍记录，未修改控制时序。
- [x] 用户已保存 .ioc，AI 完成生成前配置复查。
- [x] 用户以 c36ed99 为检查点完成原地生成；AI 完成生成差异审查与全量构建，发现 2 处编译错误及未完成的业务接入。
- [x] 按第 13 节修补重复入口、安全启动、ADC 读取、任务业务、数据交接与故障处理；全量编译0错误0警告，硬件验收另计。
- [x] 第 13 节已落地快照、实际dt、2ms初始软件预算、早到启动拒绝与复位才能清错的策略；2ms合理性和实际时序/余量仍须板测。
- [ ] 补充真实板卡版本、当前已验证供电/负载/模式、基线 ISR 最坏耗时与波形；缺少这些不妨碍当前 .ioc 审查，但不能宣称上板完成。

回退：生成前检查点为 c36ed99，关键源码与 Keil 配置均已跟踪。按文件/差异恢复需要保留的旧逻辑，不批量放弃用户修改；新增 RTOS 文件单独审查。已共享的提交使用新 revert 提交，不改写同伴历史；v1.0.0 不移动。

## 10. 官方资料与本地核查入口

- [FreeRTOS Cortex-M 中断优先级](https://www.freertos.org/Documentation/02-Kernel/03-Supported-devices/04-Demos/ARM-Cortex/RTOS-Cortex-M3-M4)：优先级门槛与移位值；本规格 5/4/15 是本工程候选分配。
- [Arm CMSIS-RTOS2 线程管理](https://arm-software.github.io/CMSIS_5/RTOS2/html/group__CMSIS__RTOS__ThreadMgmt.html)：stack_size 字节单位。
- [ST RM0433](https://www.st.com/resource/en/reference_manual/rm0433-stm32h742-stm32h743753-and-stm32h750-value-line-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)：TIM1/TIM8 中心对齐计数、时钟和内存域。
- [STM32H750 数据手册](https://www.st.com/resource/en/datasheet/stm32h750vb.pdf)：器件保证的 Flash/RAM 与引脚资料。
- [ST H7 DMA FAQ](https://community.st.com/stm32-mcus-60/dma-is-not-working-on-stm32h7-devices-97)、[AN4839](https://www.st.com/resource/en/application_note/an4839-level-1-cache-on-stm32f7-series-and-stm32h7-series-stmicroelectronics.pdf)：DMA 可达性与缓存一致性。
- [ODrive fw-v0.5.6](https://github.com/odriverobotics/ODrive/releases/tag/fw-v0.5.6)：原稿参考版本；本轮不迁入实现。
- 本地 RTOS 包：C:/Users/LEGION/STM32Cube/Repository/STM32Cube_FW_H7_V1.12.1/Middlewares/Third_Party/FreeRTOS/Source；核查 include/task.h、CMSIS_RTOS_V2/freertos_os2.h、CMSIS_RTOS_V2/cmsis_os2.c、portable/RVDS/ARM_CM7/ReadMe.txt。

## 11. 2026-11-04 配置复查与操作说明（历史记录；当前生成策略见第 12 节）

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

当时建议的副本生成步骤（已由用户后续的原地生成决定替代）：

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

## 12. 2026-11-04 原地生成后审查（历史；修复结果见第 13 节）

### 12.1 范围与证据

- 对照检查点：c36ed99（前cubeMx架构）；分支 Xiao_ODrive。本地 origin 跟踪记录显示 ahead 2，本次未 fetch，不据此断言 GitHub 最新状态。
- 用户已完成 CubeMX 原地生成。审查开始时 18 个受控文件有变更，新增 FreeRTOSConfig.h、freertos.c、TIM7 时基、FreeRTOS 中间件与 3 个 LL 头文件；保留这些用户生成结果，未暂存。
- AI 本轮只审查、执行 Keil 全量构建、更新本规格并归档日志，没有修改 .ioc、固件源文件或 Keil 工程配置；没有烧录或启动调试。
- 全量构建使用 ARMCC 5.06 update 7 build 960，耗时 2 分 44 秒，Keil 进程退出码 2，结果为 2 Error(s)、2 Warning(s)，Target not created。尚未成功链接，不使用旧 .map/.axf 宣称 RTOS 内存或固件通过。
- 命令、完整输出与关键输入 SHA256：[生成后构建记录](../validation/001-rtos-generated-review-build.txt)。

### 12.2 必须修补的问题

| ID / 分类 | 实际证据 | 影响与后续处理 |
| --- | --- | --- |
| G01 / 编译 FAIL | main.c:446 和 :553 两次定义 HAL_TIM_PeriodElapsedCallback；编译器在 :553 报 #247 | 合为唯一回调，TIM1 执行原控制链，TIM7 仅 HAL_IncTick；把手写分支放入生成回调的 USER CODE 区，避免再次生成重复 |
| G02 / 编译 FAIL | stm32h7xx_it.c:182 和 :209 两次定义 USART1_IRQHandler；编译器在 :209 报 #247 | 保留生成入口，移除旧重复定义；头文件 :56/:59 重复声明一并整理，不重复调用 HAL |
| G03 / 任务业务未接入 | main.c:261 启动调度器，原后台循环仍位于 :267 起；freertos.c:143 的 appTask 只有 osDelay(1) | 调度器成功启动后原后台循环不可达。把旋变采集、启停后台逻辑、5 ms 遥测迁入唯一业务任务；FOC 继续留 TIM1 |
| G04 / 测量映射 FAIL | current_sense.c:70 W 仍读 ADC1 JDR2，:72 母线仍读 ADC3 JDR1 | 生成的 ADC3 R1 已是 W、R2 是母线；分别改为 ADC3 JDR1 / JDR2，不能只改初始化而保留旧读数 |
| G05 / 安全启动退化 | main.c:221–227 丢失 BoardPowerStage_Init 和 BoardPWM_PinsSafeGPIO 调用；函数体仍在 :86/:105，因此构建报 2 个未引用警告 | gpio.c:54 仍正确初始化 EN 为低，不能误报 EN 未初始化；真正丢失的是校准前六路 PWM 引脚强制 GPIO 低的策略。恢复明确的启动次序，并放在可保留区 |
| G06 / 故障与创建失败处理缺失 | freertos.c:72/:81 两个故障 hook 为空，:124 不检查 osThreadNew；main.c:257/:261 不检查内核初始化/启动异常返回；FreeRTOSConfig.h:156 断言只屏蔽部分中断后死循环 | 检测开启不等于已有安全处置。补统一无阻塞故障入口、输出撤销及创建/启动失败处理；门槛 5 的 BASEPRI 不屏蔽 TIM1=0，断言死循环不能当作功率关断 |
| G07 / 并发与时序缺口 | 旋变角度/速度/valid 仍分别发布；启停请求和 power_stage_enabled 跨后台与 TIM1；见第 4.3 节 | 迁任务时定义一致快照和启停交接，不能仅搬动 while 或加入 volatile；SPI 超时和固定 1 ms 测速假设仍需落实超期处理 |

主程序在 main.c:245–250 启动 ADC/内部 TIM1 后才创建 RTOS 对象。中断早于调度器并非一概错误；但接入任务时需明确初始化依赖与失败路径，保证后台未就绪或创建失败时不会进入功率使能流程。

用生活例子理解：当前调度室和员工名单已经建好，但新员工只在等待，旧工作清单仍留在调度器接管后不再经过的位置；同时两个同名入口造成编译冲突。要完成的是职责交接和入口合并，而非继续增加任务数量。

### 12.3 已核对正确的部分

| 项目 | 生成后证据 / 结论 |
| --- | --- |
| RTOS 配置 | FreeRTOSConfig.h:68 tick1000、:71 heap16384、:77 栈检查2、:79 malloc hook1、:126 heap4、:138 lowest15、:144 syscall5；freertos.c:51–55 appTask/Normal/4096 B |
| 两套时基 / 内核入口 | TIM7 时基文件 :41 提供 HAL_InitTick，主时钟下 PSC199/ARR999，hal_conf.h:168 优先级4；cmsis_os2.c:159 提供 SysTick；SVC/PendSV 映射 RVDS port；旧空 handler 已移除。静态链路正确，需先修 G01 才能构建运行 |
| NVIC / MSP | TIM1=0、USART1=5、SysTick/PendSV=15、GROUP4；startup_stm32h750xx.s:32 MSP=0x1000，C 库 heap 仍0x200 |
| ADC 注入序列 | adc.c:98 U=ADC1 R1 CH16，:161 V=ADC2 R1 CH19，:224 W=ADC3 R1 CH10，:244 母线=ADC3 R2 CH1；均 TRGO2 上升沿、64.5cycles；:359 保留 PC3_C 模拟开关 OPEN |
| ADC1 regular / injected 采样时间 | regular 1.5cycles 先配置，随后同通道 injected 64.5cycles 写入最终采样寄存器；本地 HAL adc_ex.c:2199 支持该结论，现应用不启动 regular，未将其列为运行阻塞 |
| TIM1 | PSC1、ARR2499、中心对齐1、RCR1、CH4 PWM2/1250、TRGO2 OC4REF、死区100均保留；增加 Base_Init 与 Base_MspInit 是生成方式变化，不是定时器时钟丢失 |
| SPI / UART / GPIO | SPI1 Mode1、250 kHz、PB3/4/5；USART1 115200/8N1、D2PCLK2、PB6/7；EN 初始低、AD2S 四根控制脚初始高均正确。per_ck=HSI 设置移到 main.c:431 PeriphCommonClock_Config，并在 :214 调用 |
| Keil 模块 / 端口 | 原39个源码条目全部保留，新增12个，共51个路径均存在且无重复；CMSIS v2、heap_4、RVDS/ARM_CM4F port匹配ARMCC5。该包端口适用于本器件r1p1，不能因目录名CM4F误换端口 |
| 编译器 / 内存边界 | ARMCC5.06u7/DFP4.1.3保持；Flash128KiB、DTCM128KiB、AXI512KiB配置保持；真正分配落点和余量因链接失败尚未确认 |
| 大量工程差异 | uvoptx XML叶值无变化，属于格式；uvprojx保留原模块并增加RTOS，不按diff行数判断功能被重写 |

### 12.4 其他差异与潜在风险

- main.c:382 VOS3→VOS1、:421 Flash等待4→2已实际生成，HCLK仍200MHz。这是额外的供电/Flash档位变化，不是RTOS所必需；集成时按保持基线的原则核对并统一 .ioc 与源码，不能只改生成源码造成下次反复。
- uvprojx:81 的 AfterMake/RunUserProg2 从0变1，但 :83 命令为空；本次报错不由此引起，集成时恢复关闭无意义的构建后动作。
- 原 usart.c 中 fputc 重定向被删除。当前真实遥测使用 snprintf + HAL_UART_Transmit_IT，未发现 printf 调用，因此不判为当前遥测必然失效；如需保留该调试接口，应放入手写模块或 USER CODE 区。
- 只有“配置/静态核对”通过；任务栈与堆余量、PWM/ADC波形、中断最坏耗时、真实故障关断、旋变采集失效和数据超龄处理均未上板验证。现有旋变故障仅诊断的策略也未因引入 RTOS 自动解决。

### 12.5 后续实现顺序与职责

1. 合并 G01/G02 重复入口，保留唯一 TIM1/TIM7 分发与 USART1 handler；不通过删除控制链来消除编译错误。
2. 恢复安全初始化，在调度器和任务就绪后按已定义依赖启动采样/后台；将业务循环接入 appTask，以绝对周期等待并记录超期。TIM1 继续负责高频控制，appTask 正常独占 SPI/UART 发送缓冲区。
3. 修正 W/母线读数，并完成旋变/遥测一致快照与启停交接设计。明确每份数据由谁写、谁读、何时可用，ISR 不等待后台任务；落实固定 dt 与超时失败的处理。
4. 补齐任务/内核创建失败、堆分配失败、栈溢出、断言及已有错误入口的无阻塞关断和诊断。不能只补空函数或用死循环冒充已关断。
5. 清理额外 VOS/Flash、空构建后动作和按需保留的调试接口；保持算法、协议和无关功能不扩展。
6. 对修补后的最终差异再次全量编译，检查 Flash/RAM 与实际 heap/stack 落点；通过后才安排用户指定条件的板上时序、余量和故障验证。本次没有执行这些后续源码修改。

## 13. RTOS 最小接入实施（2026-11-04）

### 13.1 本轮授权与职责合同

用户明确要求按上一轮步骤开始执行。开始时 HEAD=996b818（CubeMX生成结果，RTOS业务待接入），Xiao_ODrive 工作区干净。实现只覆盖当前 RTOS 最小接入；不迁入 ODrive、通信协议或新控制算法，不提交/推送/烧录。

- main 的 USER CODE 负责安全初始化和应用初始化；调度器创建/启动失败必须进入故障路径，禁止回落到旧裸机业务循环。
- 新 app_runtime 模块承接原手写启动、后台与 TIM1 控制胶合代码；原电流/速度 PI 与控制参数保留。StartAppTask 只有一个业务入口，以1ms绝对周期运行旋变/后台，每5ms发送原格式遥测；超期跳过旧周期并记录，不忙追赶。
- TIM1 ISR 是正常功率 EN/MOE 的唯一操作者。任务只准备PWM通道/引脚并发布prepared，最终在短PRIMASK区复核运行请求、校准、数据有效性、故障锁存后开输出。taskENTER_CRITICAL不能屏蔽IRQ0，因此不用于这份交接。
- 旋变快照由appTask写、TIM1读，包含角度rad/速度rpm、DWT采样时刻、HAL毫秒时刻、序号、位置有效/速度有效/设备故障位。发布和读取都是有界固定字段拷贝，短PRIMASK区不包含SPI、浮点计算或格式化；ISR不自旋等待任务。
- 速度使用两次实际采样时刻差分；首次采样、SPI失败后首帧、间隔超过2ms时速度无效并重建差分基础，不能把长间隔冒充1ms。2ms是1ms发布周期+1ms交付余量的初始软件预算，必须通过板测确认，不声称由电机最高转速或安全认证推导。
- 允许使能需位置与速度快照有效且年龄不超过2ms，并有本控制拍的新ADC帧和校准ready。已经使能时输入无效/超龄/缺ADC帧撤输出并锁存，初版故障仅复位恢复，禁止故障后自动重启。设备AD2S故障位继续单独诊断；原来强制initialized=1会掩盖配置失败，应移除并采用真实回读结果。
- 反馈/遥测跨上下文读取完整副本，UART静态发送buffer由appTask独占；busy时只计丢帧，不重写在发buffer。
- 新 board_safety 模块直接撤EN、无条件清MOE；锁存故障另清CH1–3六个CCER使能，保留CH4、计数器与TRGO2配置。Fatal处理不依赖RTOS/HAL等待，覆盖创建失败、malloc、栈溢出、断言、Error_Handler和CPU异常。正常停机保留六路CCER以便下一次明确运行请求；TIM1始终复核fault才能重开输出。
- 当前只验证GPIO_EN板级方式；没有EN的ALWAYS_ON模式不自动套用此启动方案。软件关断不替代硬件保护，真实电平/延迟及MOE关闭后ADC触发持续性仍须上板验证。
- 维护FreeRTOS堆剩余/历史最低剩余、appTask最小栈余量、周期超期和ISR耗时诊断；MSP先核对链接预算，真实最坏嵌套余量另测。
- 将VOS/Flash等待恢复到裸机基线VOS3/4等待周期，保持200MHz与既有PWM参数；同步.ioc，关闭空构建后命令。手写入口放USER CODE或独立模块，避免下次生成重复。

### 13.2 验证计划（执行结果见 13.4）

1. 静态核对单一回调、任务/IRQ调用边界、所有失败入口、ADC映射、任务创建检查、UARTbuffer所有权与故障不可重启；独立复审最终差异。
2. 对周期回绕/超期、数据年龄、运行许可和故障锁存进行可在本机执行的行为检查；工具条件不足时明确缺口，不把源码搜索称作硬件验证。
3. Keil全量重建并归档日志与map，检查Flash/RAM、MSP、RTOS堆与静态/动态任务栈。
4. 板上验收仍NOT_RUN：断开功率时检查两套tick、任务周期、MOE=0时ADC更新；在用户指定条件下检查原控制功能、故障输出、电流/角度方向、最坏ISR与短临界区延迟、栈堆余量。不得因编译通过宣称电机运行通过。

### 13.3 实际修改与行为变化

| 范围 | 最终实现 |
| --- | --- |
| 生成入口 | main.c 保留唯一 TIM1/TIM7 回调；USART1 入口/声明唯一；全部手写接入位于 USER CODE，额外的生成区变化只有恢复 VOS3/FLASH_LATENCY_4 |
| 应用职责 | 新 Core/Src/app_runtime.c 与 app_runtime.h：初始化、任务启动、后台一步、TIM1控制四个入口。原 main 的手写胶合逻辑迁出；open_foc.c/.h、数学库、HAL与FreeRTOS第三方源码逐字节未改 |
| 周期与任务 | freertos.c 的 StartAppTask 1ms绝对周期；超期重设未来deadline，检查与osDelayUntil间的tick竞争用一次有界osDelay退让；保留5ms两列速度遥测 |
| 启动 | main校准/AD2S/FOC初始化；task对象创建成功且调度运行后启动ADC→CH4→TIM1。零偏ready后任务准备六路PWM，只有TIM1能最终使能EN/MOE |
| 早到启动 | 未完成准备/校准或反馈无效时收到run_request，清除请求并计 app_start_rejected_count；就绪后需要新的启动请求，避免没有功率输出时先消耗对齐计时 |
| 控制链 | 正常路径保持 RotorFeedback→20分频速度环→CurrentMonitor→CurrentFeedback→FOC 顺序；TIM1不调用RTOS；ISR预算统计改为实际9996 CPU cycles（49.98us），PWM/控制参数未改 |
| ADC | W读ADC3 JDR1、母线读ADC3 JDR2；修正头文件注释；current_monitor增加TIM1拥有的sample_sequence并将ready设volatile，用本拍是否消费新帧参与运行许可 |
| 快照与测速 | ad2s1210.c/.h 提供32B完整快照、实际DWT间隔测速；HAL毫秒时间同时排除长时间停更导致DWT整圈混淆；首帧/恢复首帧/超预算帧速度无效，保持真实状态 |
| 功率与故障 | 新board_safety模块：正常STOP撤EN/MOE，锁存FAULT另清六路CCER，保留CH4与计数；栈/堆/断言/内核/任务创建/初始化/CPU异常均接入。初版FAULT只允许MCU复位清除，不提供运行中清错API |
| 工程 | Keil新加app_runtime.c、board_safety.c，原模块保留，53个源码条目存在且唯一；空AfterMake命令关闭；.ioc与main恢复基线VOS3 |
| 验证策略 | app_policy.h集中周期、年龄与许可宏；tests/rtos_policy_compile_checks.c含85个编译期场景（64种许可组合、19个时间/回绕场景、2个布局/预算检查） |

关键新行为必须在上板前理解：

- AD2S配置失败会真正阻止启动，不再无条件把initialized设为1。实际条件包括配置事务成功、CONTROL=0x7F、EXC低7位=0x28和原四个阈值回读一致；这可能暴露旧调试代码曾掩盖的通信问题。未新增传感器物理阈值。
- 已使能时，旋变事务无效/速度无效/年龄超2ms或本控制拍缺ADC帧会立即撤销软件输出并锁存；再次写run_request不能绕过fault，需先排查再复位。
- AD2S硬件故障位仍为诊断-only，尚未实现完整旋变/母线/过流等保护状态机；本次代码成功不代表功率级保护已经齐全。
- 当前只支持既有GPIO_EN方式或完全禁用输出；ALWAYS_ON编译拒绝，因为没有EN时需要重新论证准备PWM期间的安全行为。
- SPI超时仍保持原HAL 10ms上界。控制侧按2ms预算独立拒绝陈旧数据；任务返回后记录超期并重建测速基准，不会把超时误当一个1ms样本。
- 临界区统计量记录主体，并非完整关中断/IRQ响应延迟。反汇编已确认快照复制中有PRIMASK保存/恢复与固定拷贝，无调用/等待循环；最坏延迟必须板测。

### 13.4 已执行验证和真实内存账单

证据：[接入后构建与静态验证记录](../validation/001-rtos-integration-build.txt)。

- BUILD PASS：ARMCC5.06u7 build960 / Keil全量重建，0 Error(s)、0 Warning(s)，2分55秒。生成了新.axf/.hex。
- 源码审查 PASS：独立复审最终启动/数据/使能/失败路径；四个入口各一份；生成区域比较确认main/freertos/it/FreeRTOSConfig的手写接入在USER CODE内，时钟恢复作为明确例外。
- 编译期场景 PASS：使用现有ARMCC编译85个生产策略宏场景；不是执行MCU或RTOS。未在PATH/常见位置找到原生Windows C编译器，未下载工具，未把编译期检查称作本机运行测试。
- 反汇编审查 PASS：AD2S1210_GetSnapshot/PublishSnapshot有CPSID、8字段拷贝、DMB和保存值恢复PRIMASK；MOE关断为无条件寄存器清位，未使用受CC4E影响的条件宏。
- HOST执行 / HW：NOT_RUN。数据交错实测、真故障注入、PWM/ADC时序、物理停机及内存余量未执行。
- 在当前200MHz时钟下，板上必须测最大控制执行时间与IRQ延迟；两者总预算不能超过实际约49.98us的控制周期，并需确认真实PWM更新截止点。仅看平均耗时不足。

| 内存项目 | 本次map结果 |
| --- | --- |
| Flash ROM总量 | 59576 B，58.18 KiB / 128 KiB；相对裸机基线增加13292 B |
| RW+ZI总量 | 25336 B，24.74 KiB，全部在AXI RAM；DTCM区域本次分配0 |
| FreeRTOS ucHeap | 0x240012F8，16384 B；动态appTask的4096 B栈及TCB从此池分配，不再额外加算4096 B静态RAM |
| MSP | 0x240052F8–0x240062F8，4096 B；初始SP=0x240062F8 |
| Idle / Timer静态内存 | Idle栈512 B、TCB92 B；Timer栈1024 B、TCB92 B，均在AXI RAM |
| C库堆 | startup仍配置512 B，但本次链接器剔除了未使用的HEAP段，因此没有实际占用；与RTOS堆分开 |
| 运行余量 | app_stack_min_free_bytes、app_heap_free_bytes、app_heap_min_free_bytes已接入诊断，需运行后读取；编译时不能填入“剩余足够”的结论 |

调用图虽显示全局最大424 B、StartAppTask已知链216 B，但同时明确包含Unknown(Cycles, Untraceable Function Pointers)，也未覆盖真实异常嵌套。不能据此缩减4KiB任务栈/MSP或宣称余量已验收。

### 13.5 交给用户的后续验证顺序

1. 先审阅本节与代码差异，按既定手动Git流程保存代码接入版本；当前没有自动提交/推送。
2. 先在功率输出隔离条件下检查：初始化结果、两套tick、app_task_iterations增长；foc_run_request保持0，EN低/MOE=0时monitor_sample_sequence继续增长并完成零偏；这验证关输出没有误停ADC触发。
3. 读取board_fault_mask/board_first_fault/board_fault_ipsr定位失败；若初始化失败，结合ad2s_init_status和寄存器回读排查，不直接把initialized/fault改成“正常”来绕过检查。
4. 检查新鲜度、app_task_deadline_misses、app_start_rejected_count、ISR最大耗时、临界区测量与堆栈余量。首次未到诊断周期时app_stack_min_free_bytes为0，不能误判已溢出。
5. 根据用户明确提供的板卡、供电、限流、负载和测量条件安排原控制功能与故障验证；本轮没有代为烧录或上电。完成此基线后再选择ODrive第一个小模块，当前可以并行做源码/接口对照。

## 14. 2026-11-05 首轮实板检查说明（检查方法；首批结果见第15节）

### 14.1 本轮边界和准备

用户请求指导如何检测。本轮只核对源码/构建指纹、调试设置和准备检查表，不下载、不启动调试、不连接电源。现源码与上一轮成功构建的21项指纹全部一致；Git仍未提交本轮接入改动，HEAD=996b818。

硬件状态：用户已确认“功率母线已断开，仅控制与传感器供电”。这是用户报告的接线状态；本节编写时尚无实测结果，后续截图观察见第15节。以下首次运行步骤还要求储能按现有硬件规程确认已释放、控制与传感器按板卡既定方式供电。foc_run_request=0仅是软件请求，不替代功率隔离。

现Keil目标是真实ST-Link目标（不是模拟器），启用了Load Application/Run to main和UpdateFlashBeforeDebugging。因此进入调试可能触发下载/运行初始化，不能视为纯查看动作。用户已报告上述隔离供电条件；本轮由用户按下述步骤操作，助手未执行下载/运行。

### 14.2 在上述条件满足后的Keil操作

1. 打开现有 MDK-ARM/FOC_T.uvprojx，target选择FOC_T。现产物已核对来自0错误0警告构建；若用户又修改源码，需重新构建并确认通过后使用新产物。
2. 用Debug → Start/Stop Debug Session（Ctrl+F5）进入调试，确认加载当前工程产物；失败时保存Keil完整下载/连接错误。
3. 暂停在main时打开View → Watch Windows → Watch 1，加入下表变量。启用View → Periodic Window Update，采用运行中观察。静态变量找不到时可在View → Symbols定位并加入；不要为了读数在Watch调用函数或写赋值表达式。
4. 本轮保持foc_run_request=0。确认没有测试断点后按Run（F5），连续运行约10秒，再在不暂停CPU的情况下间隔约5秒记录两次读数。
5. 若故障码非0、输出标志意外为1、或计数不增长，停止本轮判定并回报读数。初始化失败时可在已隔离功率条件下暂停查看当前位置/调用栈，不能改状态变量绕过失败。
6. 若中途打断点、单步、暂停或重新下载，丢弃这段时序统计，从复位后的连续运行重新记录。暂停会改变外设/CPU/DWT/软件tick的相对行为。

### 14.3 Watch 1：启动、调度和采样

| 变量 | 预期 / 含义 | 实测 |
| --- | --- | --- |
| foc_run_request | 始终0，本轮不请求启动 | 待测 |
| board_output_enabled | 始终0；仅是软件状态，不代替实际EN/MOE确认 | 待测 |
| board_fault_mask | 正常启动后0；非0则记录十六进制值 | 待测 |
| ad2s_initialized | 初始化后1；若0且fault=0x80，检查初始化回读 | 待测 |
| uwTick | HAL的毫秒计数，应持续增长 | 待测 |
| xTickCount | FreeRTOS的tick计数，应持续增长；比较增量，不要求与uwTick数值相等 | 待测 |
| app_task_iterations | 后台循环计数持续增长，正常约1000次/秒 | 待测 |
| monitor_sample_sequence | 新ADC帧计数持续增长，正常约20008次/秒；MOE低时也应继续采样 | 待测 |
| monitor_ready | ADC零偏校准结束后1；静态变量位于current_monitor.c | 待测 |
| ad2s_position_data_valid | 周期位置读取正常时1；不等于所有设备故障位均正常 | 待测 |

初始化含HAL延时，特殊诊断路径可多等约1秒；零偏约6096个采样，即采样启动后约0.305秒，不能从按F5的瞬间直接计这个时间。运行稳定后，两次读数相隔约5秒时，两个tick各约增5000，后台约增5000，ADC约增100040。手动计时/窗口刷新只是连通性粗检，不是频率或抖动精测。

若正常Watch无法解析xTickCount/monitor_ready，报告提示或从Symbols找到对应tasks.c/current_monitor.c变量；不要在运行中调用osKernelGetTickCount()/CurrentMonitor_IsReady()替代观察。

### 14.4 Watch 2：第一轮通过后看余量与时序

| 变量 | 判读方式 |
| --- | --- |
| app_stack_min_free_bytes | 每约1秒更新，单位字节；启动首秒内0不代表已经溢出。记录具体值，不能仅凭大于0就认为余量合格 |
| app_heap_free_bytes / app_heap_min_free_bytes | 当前/历史最低剩余字节；记录两者，不按16KiB直接推断运行后剩余 |
| foc_isr_budget_cycles | 当前9996，200MHz下49.98us |
| foc_isr_max_cycles / foc_isr_overrun_count | 无暂停的一段运行中看最大耗时、超预算计数；cycles/200得到微秒。仅函数内部耗时不包含完整异常进出和最坏IRQ延迟；run_request=0的路径较轻，不能代表真实闭环运行的最坏耗时 |
| app_task_deadline_misses | 稳态观察是否持续增长；若增长，结合后台/采样最大耗时调查，不直接放宽预算 |
| app_task_max_cycles / ad2s_acquisition_max_cycles | 后台/采样耗时；cycles/200转微秒，周期仍由实际tick/DWT记录核对 |
| ad2s_sample_gap_count / ad2s_spi_error_count | 稳态新增长说明采样间隔或通信存在问题 |
| app_adc_missed_count / app_feedback_invalid_count | 初始化可能已有计数；记录稳定运行区间的增量，不机械要求启动累计值为0 |
| app_critical_max_cycles / ad2s_snapshot_max_cycles | 屏蔽区主体统计；完整最大IRQ延迟仍需其他测量，不能直接等同这两个值 |

若board_fault_mask非0，追加board_first_fault、board_fault_ipsr。0x80对应传感器初始化不满足，可再读ad2s_init_status、ad2s_control_readback、ad2s_exc_readback、ad2s_threshold_config_status。HAL_OK状态值为0，成功布尔标志ad2s_initialized为1，二者含义不同。

本轮不修改foc_run_request为1，不做故障注入，不测试带电旋转或动态断点。必须先得到本表读数和实际硬件条件，再安排下一轮验证。

资料核对：Keil [Debug菜单](https://www.keil.com/support/man/docs/uv4cl/uv4cl_ui_debug.htm)、[Watch窗口](https://www.keil.com/support/man/docs/uv4/uv4_db_dbg_watchwin.asp)、[启动调试](https://www.keil.com/support/man/docs/uv4/uv4_db_dbg_startdebug.asp)。本节编写时实测状态为NOT_RUN；后续执行结果以第15节为准，未覆盖项目仍待验证。


## 15. 2026-11-05 用户实板截图复核（初步运行检查通过，完整验收待完成）

### 15.1 条件、证据与结论边界

用户前一轮确认功率母线断开，仅控制与传感器供电；本轮表示未按固定秒数记录，连续放置一段时间后提供两张Keil Watch截图。截图1文件名为codex-clipboard-892be96e-4287-44d0-93ed-5541ec304ea3.png，截图2为codex-clipboard-1d6848ae-bf45-442a-b23a-943e414d651f.png。下面转录可见数值，不把两张图当成严格同步快照。

结论：所示运行状态支持“母线断开、未请求输出的首次启动/调度/采样基本检查通过”。未发现所示诊断量报告故障或内存紧张。尚无连续两次计数差、独立墙钟/波形测量、完整运行日志或板上程序指纹回读，因此不将单次截图等同于持续稳定性、精确频率、最坏时序或全部保护验收通过。

助手本轮只核对源码和截图、更新本spec；未修改固件、重新构建、下载、运行调试或提交Git。当前分支Xiao_ODrive，接入改动仍未提交；用户调试引起的MDK-ARM/FOC_T.uvoptx修改保留。

### 15.2 截图1：启动与计数

| 变量 | 截图十六进制值 | 十进制 / 判读 |
| --- | --- | --- |
| foc_run_request | 0x00 | 0，未请求电机启动 |
| board_output_enabled | 0x00 | 0，软件输出许可关闭；不是实际EN/PWM电平测量 |
| board_fault_mask | 0x00000000 | 0，没有锁存故障；停机时部分输入异常只累计诊断，不据此推断全部输入始终有效 |
| ad2s_initialized | 0x01 | 1，初始化标志通过 |
| uwTick | 0x0000A643 | 42563 |
| app_task_iterations | 0x0000A19D | 41373 |
| monitor_sample_sequence | 0x000CA1A0 | 827808 |
| ad2s_position_data_valid | 0x01 | 1，该观察时刻位置读取有效，不代替速度新鲜度/设备故障位检查 |
| monitor_ready | 0x01 | 1，零偏校准流程完成，不代表零偏绝对精度已测 |
| xTickCount | 0x0000A19F | 41375 |

按当前1000Hz tick换算，RTOS累计计时约41.375s，HAL约42.563s；这是软件计时换算，非独立实测墙钟时长。HAL领先1188 tick，与HAL先启动及初始化延时相容，不凭差值认定时钟漂移。后台41373次与RTOS41375 tick接近，ADC帧/tick=827808/41375≈20.0074，与约20.008kHz采样和1kHz tick的设计关系相符。这是累计量的一致性检查；Watch刷新并非跨变量原子快照，仍需稳态增量确认。

### 15.3 截图2：内存余量和已记录耗时

| 变量 | 截图值 | 换算及判读 |
| --- | --- | --- |
| app_stack_min_free_bytes | 0x00000E20 | 3616 B，约占appTask 4096 B栈的88.3%；高水位估算在已执行路径下余量较大 |
| app_heap_free_bytes | 0x00002F88 | 12168 B，RTOS堆当前剩余 |
| app_heap_min_free_bytes | 0x00002F88 | 12168 B，RTOS堆历史最低剩余，约占16384 B的74.3% |
| foc_isr_max_cycles | 0x0000113A | 4410 cycles；按当前200MHz换算22.05us，占9996 cycles预算约44.1% |
| foc_isr_overrun_count | 0x00000000 | 0，当前统计未记录控制函数超预算 |
| app_task_deadline_misses | 0x00000000 | 0，当前统计未记录后台任务超期 |
| ad2s_spi_error_count | 0x00000000 | 0，当前统计未记录SPI错误 |

对应实现：Core/Src/app_runtime.c的AppRuntime_BackgroundStep以uxTaskGetStackHighWaterMark乘sizeof(StackType_t)给出字节余量，并读取heap_4当前/最低剩余；AppRuntime_FastISR在函数内部记录DWT周期差。Core/Src/current_monitor.c的CurrentMonitor_Run_20kHz只在获取新ADC帧后增加monitor_sample_sequence。

这些内存量分别是appTask栈和FreeRTOS堆，不覆盖MSP中断栈、其他任务栈或全部MCU RAM。两项堆余量相等与当前固定分配方式相容，不能单凭一帧证明无泄漏。保持现有栈堆分配，暂不缩减。

22.05us仅为本次未请求功率输出路径下已记录的函数内部最大值，不包含完整异常进出/所有IRQ延迟，也不代表真实闭环/故障路径的最坏值。截图未提供是否中途暂停的完整记录，因此时序结论保留此限制。

### 15.4 最小补测与仍待完成项

保持母线断开、foc_run_request=0，在连续运行且不暂停/单步的条件下，记录两次同一组Watch；不必精确掐5秒，可用xTickCount的差值作相对时间基准。观察uwTick与xTickCount增量接近、app_task_iterations增量与xTickCount接近、monitor_sample_sequence增量约为xTickCount的20.008倍；该方式仍不是独立绝对频率测量。

追加app_adc_missed_count、app_feedback_invalid_count、ad2s_sample_gap_count并记录两次值。启动期允许已有累计值，关注稳态是否继续增加。即使board_fault_mask=0，停机期间的反馈/采样缺帧仍可能只计入这些诊断；首次两张截图未提供它们的读数；用户随后补充的读数与观察见15.5，按实际证据更新判定。

尚待：反馈新鲜度及旋变设备故障位、实际EN/MOE/PWM与ADC触发波形、ADC量值/极性、完整IRQ延迟、MSP和最坏任务路径栈余量、真实故障关断以及受控条件下原闭环功能。完成上述软件增量补测后再安排下一轮硬件验证，不以本次截图作为接通高压母线的依据。


### 15.5 启动反馈计数补测（用户观察）

用户提供截图codex-clipboard-7df86666-3f14-4ed1-96fa-4f407c5aee11.png，并说明第二项“刚运行开始变了一下，后面就没变化了”。沿用其已报告的功率母线断开、仅控制与传感器供电条件。

| 变量 | 截图值 | 用户报告与判读 |
| --- | --- | --- |
| app_adc_missed_count | 0x00000000 | 0，未记录ADC缺帧 |
| app_feedback_invalid_count | 0x00000016 | 22，用户观察仅启动时增加，之后稳定 |
| ad2s_sample_gap_count | 0x00000000 | 0，未记录旋变采样间隔异常 |

源码依据：Core/Src/app_runtime.c:253–258每次TIM1检查位置有效、速度有效和新鲜度，任一不满足就增加app_feedback_invalid_count；此值不是SPI错误次数。Core/Src/ad2s1210.c:181–188的首次位置采样只建立差分基准，speed_valid保持0；取得下一次有效位置及合理采样间隔后才可发布有效速度。

因此，启动时高频控制先运行、速度需要至少两次有效位置才能形成，与这次“短暂累加后稳定”的现象相容。22个约49.98us检查拍折合约1.10ms的检查量，不代表已精确测得一段连续1.10ms的无效窗口，也不能据此唯一归因于首次测速。启动次数无需固定为22，不修改有效性门槛或清零掩盖计数。

判定：依据截图和用户连续观察，当前未观察到持续反馈无效、ADC缺帧或旋变采样间隔异常；此项初步观察可接受。有效判定的前提是同时看到app_task_iterations/monitor_sample_sequence等运行计数继续增长，排除CPU暂停或Watch未刷新的假象。本次补图未包含这些运行计数的前后值，精确周期增量及完整时序验收仍待完成。

本次仅更新spec记录，固件不变。检查发现用户已暂存当前集成改动；助手不改暂存区，本节新记录在工作区，用户提交前需将最新spec重新暂存。
