# 002 — ODrive Axis 组织方式的第一步：运行状态观测

**一句话：** 增加只读运行状态字，让用户看到程序阶段和现有阻止原因；它不接管启停或PWM。

**状态：** 已实现；核对版本1668486＋002工作区实现，源码/目标产物指纹见[003构建记录](../validation/003-architecture-build.txt)。当前分工见[架构页](../ARCHITECTURE.md)，日常入口见[README](../README.md)。

| 改了哪里 | 改前 → 改后 | 为什么 | 谁执行、交给谁 | 验证情况 |
| --- | --- | --- | --- | --- |
| axis_status.h/.c | 多个零散量 → 单个32位观测字 | 同一读数解释阶段/原因 | TIM1正常发布 → 诊断读者 | 源码/编译期检查PASS；待机截图观察PASS |
| app_runtime.c | 输出决策后无统一观测 → 发布实际软件结果 | 不把提前算出的许可当实际输出 | FastISR → AxisStatus_Publish | 真实运行态与最坏耗时仍待测 |
| board_safety.c | 故障锁存 → 撤输出后补充FAULT观测 | Fatal也可留下诊断 | LatchFault → AxisStatus_LatchFault | 源码/编译PASS；故障注入NOT_RUN |

| 检查项 | 层级 | 结果 | 版本与证据／边界 |
| --- | --- | --- | --- |
| 发布顺序、无控制权 | 源码检查 | PASS | [002构建/静态记录](../validation/002-axis-status-build.txt) |
| 27项策略检查 | 编译期检查 | PASS | 同上；不是运行测试 |
| 当前目标构建 | 编译 | PASS | [003全量重建](../validation/003-architecture-build.txt)；本次纯文档不重复编译 |
| HOST运行行为 | 主机 | NOT_RUN | 未执行；不以编译期检查代替 |
| 待机/软件输出/任务采样推进 | 板测 | PASS | [首组截图](../validation/002-axis-status-bench.md)，有限观察，未回读板端产物指纹 |
| 三项异常计数未新增 | 板测 | PASS | [第二组记录](../validation/002-axis-status-bench.md)，单截图＋用户报告；不是精确双端点测量 |
| 停止路径函数内耗时、任务栈/堆余量观察 | 板测 | PASS | [第三组记录](../validation/002-axis-status-bench.md)，有限观察，不能代表最坏工况 |
| 故障态、运行态、物理输出及最坏资源充分性 | 板测 | NOT_RUN | 保留后续分级验收，不借有限观察代填 |

**当前阶段判定：** 停止状态基础观察已收尾，可进入下一模块纯软件命令边界开发，不再为此追加Watch参数。完整闭环、故障态与最坏时序/资源验收仍未完成，见第8.8节及README未关闭事项。

## 功能设计与原实施记录

- 日期：2026-11-06；基线：1668486（接入RTOS，keil测试通过），Xiao_ODrive；开始时工作区干净。
- 原实施时状态：只读观测已实现，源码复审与Keil构建通过，当时待实板复测。后续有限观察见前部验收表及第8.6/8.7节；仍不是完整Axis控制器或完整实板验收。
- 用户请求：RTOS已提交并测试，继续推进移植；沿用既有授权，不增加默认审批关口。

## 1. 本轮目标与范围

先让现有程序回答“当前在哪个阶段、哪些现有条件不满足”，再接统一命令。像给已有设备装状态指示牌，指示牌没有开关电源的权力。

现有FOC_ControlState的0/1/2分别是电流、速度+电流、旋变开环测试模式，不是初始化/待机/运行/故障生命周期。现有foc_run_request、foc_aligning、power_pwm_prepared、monitor_ready和board_fault_mask共同决定执行行为。本轮将这些事实形成只读观测；原控制仍拥有唯一决定权。

新增axis_status.h/.c和一个32位状态字，正常路径在TIM1最终输出决策之后发布。故障路径在先撤输出后更新FAULT，即使Fatal停住调度器也能观察。保持原PI、状态转换、PWM/ADC、RTOS任务、遥测格式、输出许可和保护阈值。不开新任务，不动态分配，不接收新运行命令，不增加自动启动/清错，不操作硬件或Git提交。

## 2. 官方参考与本板适配

参考固定为ODrive fw-v0.5.6 / a308314ed2ca613164b81e7bbdfacc53cd1859ff，2026-11-06使用GitHub连接器读取：

- [axis.hpp](https://github.com/odriverobotics/ODrive/blob/a308314ed2ca613164b81e7bbdfacc53cd1859ff/Firmware/MotorControl/axis.hpp#L202)：requested_state与current_state分开。
- [axis.cpp](https://github.com/odriverobotics/ODrive/blob/a308314ed2ca613164b81e7bbdfacc53cd1859ff/Firmware/MotorControl/axis.cpp#L455)：状态请求、执行阶段和失败返回路径分开；arm/disarm是另外的实际动作。
- [LICENSE.md](https://github.com/odriverobotics/ODrive/blob/a308314ed2ca613164b81e7bbdfacc53cd1859ff/LICENSE.md)：上游许可证来源。

本轮是参考职责分离后为现有C工程独立实现的观测模块，没有复制上游实现或枚举值；不称为已移植ODrive原版Axis。后续若复制源码，必须保留其版权与许可。本板保留TIM1 IRQ0实时控制、appTask后台采集、board_safety先关输出的结构，不照搬ODrive的Axis线程、信号同步、校准参数或默认阈值。

## 3. 接口与状态语义

单个volatile uint32_t axis_status_word：低8位为状态，高24位为inhibit位图。通过一次32位读取获得同一份状态/原因；Watch用axis_status_word & 0xff查看阶段，axis_status_word >> 8查看原因。不提供写命令接口。禁止在Watch修改该变量。

| 值 | 状态 | 含义 |
| --- | --- | --- |
| 0 | BOOT | 尚未发布首个控制周期状态；这时原因位不作就绪判据 |
| 1 | CALIBRATING | ADC电流零偏未完成；不是电机参数/旋变电角零位的完整校准 |
| 2 | IDLE | 软件输出关闭，零偏已完成；是否具备其他启动条件看原因位 |
| 3 | ALIGNING | 软件输出已允许，原FOC对齐标志为1 |
| 4 | RUNNING | 软件输出已允许，对齐标志为0；包含开环测试模式，不证明电机转动或闭环性能 |
| 5 | FAULT | 现有故障已锁存；诊断记录也保持至MCU复位 |

FAULT优先；否则有输出时按对齐标志区分ALIGNING/RUNNING；无输出时按零偏区分CALIBRATING/IDLE。foc_aligning停机时也可能为1，不能单凭它报告对齐。control mode仍从foc_control_state_active读取，和本状态字不是原子组合，不作跨字段控制依据。

原因位图（是word右移8位后的数值）：

| 位值 | 含义 |
| --- | --- |
| 0x001 | 六路PWM尚未准备 |
| 0x002 | ADC零偏尚未完成 |
| 0x004 | 位置样本无效 |
| 0x008 | 速度样本无效（启动首帧也可出现） |
| 0x010 | 反馈时间戳不满足现有2ms新鲜度条件 |
| 0x020 | 本控制拍未取得新ADC帧 |
| 0x040 | 现有测试锁定标志启用 |
| 0x080 | 现有故障锁存 |
| 0x100 | 编译配置完全禁用功率输出 |

未收到run_request是正常待机，不列为异常。原因位0只表示本轮已实现的软件条件就绪，不代表未知的母线、温度、硬件保护条件已经验收，也不会自动开输出。设备AD2S故障位仍为原有仅诊断策略，不因状态字为IDLE就变成“所有传感器故障正常”。

## 4. 执行上下文、所有权与失败

- C启动：静态初始化为BOOT；初始化失败经board_safety进入FAULT。
- TIM1：复用当前ADC/位置快照/实际时刻产生原因位，在既有PRIMASK保护区内、BoardSafety_SetOutputsEnabled之后，读取实际board_output_enabled及最终board_fault_mask发布，不从提前计算的allow推断已输出。无HAL/RTOS等待或分配。
- BoardSafety_LatchFault：原撤EN/MOE、禁CH1–3及锁存顺序保持，随后在原PRIMASK内更新状态字为FAULT；所有故障原因在原board_fault_mask保留，不复制第二套故障控制器。
- 状态字的FAULT记录保持，后续普通发布不能把它改回IDLE；无清错函数。
- 读者：Keil或未来诊断消费方只读取状态字；生产控制不读取该字作启动许可，避免循环依赖。
- 共享发布不扩展为多字段seqlock，不让高优先级ISR等待后台任务。统计增加的ISR耗时，现有max_cycles计量必须包含新观测工作。

## 5. 本轮验证计划

1. 编译期检查状态优先级、待机+对齐标志、输出状态、故障保持和打包/解包；这不是HOST运行测试。
2. 独立源码审查：状态无控制权；正常/故障发布均在既有临界区；Fatal不依赖调度器；BOARD_POWER_STAGE_DISABLED不会虚报RUNNING；open_foc.c/.h控制算法及外设配置不变；app_runtime.c只增加观测接入。
3. Keil编译链接通过，记录ROM/RAM增量和新增状态字大小；只构建，不下载/启动调试。
4. HW待用户执行：在已明确隔离的供电条件下复位连续运行，稳定时phase=2、inhibit=0，输出/运行请求仍为0，任务/ADC计数增长；与001基线比较ISR max和诊断余量。不要为观察3/4而开启电机。
5. FAULT及带输出阶段先通过策略编译检查和代码复审；实际故障注入、物理输出、原闭环最坏耗时仍单独待测，不冒充完成。

## 6. 后续推进顺序与已知限制

- 本轮：只读运行状态与已实现条件的阻止原因，准确记录基线；修正文档中过时ADC/遥测/使能说明。
- 下一模块：统一单源命令接受/应用/拒绝记录，停止/故障优先和旧请求失效；再根据实际需求扩展控制权和通信。先定义交接合同，再获得命令控制权。
- 同步硬件验证：实际EN/MOE/PWM、ADC相位与量值、旋变设备故障/方向、临界区和MSP余量。现有软件STOP是撤输出，不宣传为受控减速制动。
- 尚无已核实的保护阈值/功率级完整资料时，不新造过压、过流、制动等可运行默认参数。

## 7. 原实施时验证结果（后续板测见前部验收表）

证据：[002构建与静态验证记录](../validation/002-axis-status-build.txt)。

- SRC PASS：独立复审未发现阻塞项；新增axis_status.h/.c、app_runtime中最终结果发布、board_safety在关输出之后发布FAULT。只有观测模块读写状态字，没有控制逻辑反向读取。
- 编译期检查 PASS：27个场景，含待机时align标志为1、停止、故障覆盖所有阶段、故障保持、原因恢复和打包解包；不是HOST或MCU执行。
- BUILD PASS：ARMCC5.06u7 build960，Keil增量构建0错误0警告，18秒；54个源码路径存在且唯一。未为这次变更重新生成CubeMX。
- MAP PASS：ROM 59812 B，比1668486增加236 B；RW+ZI 25344 B，增加8 B（含对齐变化）。axis_status_word在0x24000000，占4 B。
- 反汇编 PASS：两个发布函数各自以单个32位STR写回状态字，函数内部没有其他调用；发布点沿用原PRIMASK保护。开关输出仍是原board_safety职责。
- 兼容性检查 PASS：open_foc算法、ADC/TIM、freertos任务、FreeRTOSConfig、.ioc、HAL/内核源码均与1668486相同；新增工作已计入原ISR及临界区耗时诊断。
- 文档更正：PORTING_H750.md按1668486事实重写，纠正W/VBUS映射、遥测、启动及时间周期；board_config.h仅修正过时注释，宏配置未改。
- HOST NOT_RUN：未找到可直接使用的本机C编译器，未安装工具；不把编译期检查称为运行测试。
- HW NOT_RUN：新构建未下载、未运行。001中的3616/12168 B与22.05us属于旧基线观察，不能直接填成新版本实测。
- Git：本轮改动留在工作区，没有暂存、提交、推送。

## 8. 用户下一步：002待机状态观测＋60秒稳定性复测（2026-11-06更新）

**执行方式更新：用户要求分轮少看参数，每轮仅3–4项。下列完整清单保留作备查，不要求一次添加；首组结果与下一组见第8.6节。**

本轮沿用用户已明确的功率母线断开、仅控制与传感器供电条件，以及既定余电处理。只测新增观测模块、当前停止路径的调度/采样/内存和cache使能状态；不请求运行，不注入故障，不修改故障/状态字，不测试DMA（目前没有业务DMA）。硬件条件有变化时不照搬本表。

### 8.1 产物与操作

当前工作区的002实现及003全量重建产物已核对，14项SHA与[003构建记录](../validation/003-architecture-build.txt)匹配。AXF为`MDK-ARM/FOC_T/FOC_T.axf`，SHA256：`2B66DA04348820D688DAD6B107DED802A245F2511E52CAA75C7B567DD970D9A5`。没有重新生成CubeMX或改变固件。用户若再改源码，应重新构建并记录新产物，不能沿用此SHA或旧板测结果。

1. 打开`MDK-ARM/FOC_T.uvprojx`，选择FOC_T。使用当前构建产物进入真实ST-Link调试，参照001第14节；现设置进入调试可能下载并运行至main。由用户操作，助手本轮未下载/运行。
2. 在Watch逐行加入下面变量；斜杠并列名称不是除法表达式。静态变量查不到时从Symbols定位，不在Watch调用函数。
3. 复位后无断点、无单步地Run，约10秒后记录T0；再连续运行约60秒记录T1。使用运行中的窗口刷新，截图不暂停CPU。
4. 中途暂停/断点/下载会污染时间统计；这种数据保留作调试线索，另从复位后的自由运行重新采集验收记录。
5. 填写[002台架记录](../validation/002-axis-status-bench.md)，或提供T0/T1两轮完整Watch截图。每轮包含状态、错误计数、内存与耗时；未知值保持待测。

### 8.2 基础状态和计数（每个变量单独加入Watch）

| Watch表达式 | 预期 / 判读 |
| --- | --- |
| axis_status_word | 稳定停止状态0x00000002；低8位为2=IDLE，高位为0 |
| foc_run_request | 0 |
| board_output_enabled | 0；软件标志，不替代实测门极电平 |
| board_fault_mask | 0 |
| ad2s_initialized | 1 |
| monitor_ready | 1 |
| ad2s_fault | 记录原值；非0需单独解释，不能由axis inhibit=0推断器件全正常 |
| uwTick | 两次增量约60000 |
| xTickCount | 两次增量约60000；与HAL起点不同，不要求绝对值相等 |
| app_task_iterations | 两次增量约60000，与ΔxTickCount接近 |
| monitor_sample_sequence | 两次增量约1200480，或约20.008×ΔxTickCount |

优先从**同一个axis_status_word原始值**解码状态/原因。`axis_status_word & 0xff`和`axis_status_word >> 8`可以辅助观察，但Watch各行分时刷新，不是原子组合。状态字是瞬时观测，不是历史事件日志；还要结合累计错误增量判断。

手动计时和刷新不同步，因此上述增量用于粗检，不要求精确等于目标，也不编造精确容差。计数不增长或数量级不对时排查；频率/抖动验收仍需独立测量。增量采用uint32差值，即(T1−T0)模2^32。

### 8.3 稳定区间错误计数

以下逐个记录T0/T1，稳定区间预期**增量均为0**：

```text
app_adc_missed_count
app_feedback_invalid_count
ad2s_sample_gap_count
ad2s_spi_error_count
foc_isr_overrun_count
app_task_deadline_misses
app_start_rejected_count
ad2s_acquisition_over_budget_count
```

启动时app_feedback_invalid_count曾为22等非零累计值，不要求清零；关键是稳定之后不再新增。ISR超预算/任务超期从干净启动宜为0，若已有非零也记录并调查，不能只挑稳定区间掩盖启动问题。任何稳定区间新增错误，先保留数据并定位，不放宽预算、不清故障换取PASS。

### 8.4 内存、耗时与cache状态

| Watch表达式 | 单位和判读 |
| --- | --- |
| SystemCoreClock | 当前应为200000000；cycles/200得到微秒 |
| foc_isr_budget_cycles | 当前9996＝49.98us |
| foc_isr_max_cycles | 记录本构建最大值；需小于预算并保留余量。代码只在>预算时计数，等于预算也没有余量 |
| app_critical_max_cycles | 当前应用临界区主体最大cycles，先记录，不虚构完整关中断门槛 |
| ad2s_snapshot_max_cycles | 旋变快照读/写临界区主体最大cycles |
| ad2s_sample_edge_max_cycles | SAMPLE边沿/时间戳临界区主体最大cycles |
| app_stack_min_free_bytes | appTask历史最小剩余字节，约每秒更新；记录T0/T1 |
| app_heap_free_bytes | RTOS当前剩余字节 |
| app_heap_min_free_bytes | RTOS历史最低剩余字节；与栈不要重复计入静态RAM |
| ad2s_acquisition_max_cycles | 采集elapsed最大值，可能包含IRQ抢占 |
| ad2s_acquisition_budget_cycles | 当前160000＝800us，仅现有软件采集预算 |
| ad2s_sample_interval_max_cycles | 采样间隔历史最大值；通常约200000cycles/1ms，当前>400000/2ms判gap |
| *((unsigned int *)0xE000ED14) | 只读SCB CCR；bit16/DC=1表示D-cache开启，0表示关闭；本源码无显式enable调用，记录真实会话值 |

CCR可在内核寄存器窗口读取；Watch若不能解析上述指针表达式，不调用函数替代。把完整十六进制值交给审阅者解码即可，不写寄存器。

三类临界区统计未覆盖全部进入/退出/统计开销和所有故障路径；不能直接相加当作精确IRQ延迟。foc_isr_max也不是完整异常响应时间。本轮run_request=0走停止路径，不能据此判断带输出闭环的最坏耗时通过。内存余量为正不等于最坏场景足够，001旧值22.05us、3616B、12168B只作对照，不填为新值。

### 8.5 分轮前准备状态与后续（后续实测见8.6/8.7）

- 本清单准备时：SRC/BUILD沿用已记录证据，当时只做文档/产物核对，HW为NOT_RUN。后续用户结果在8.6/8.7追加；当前验收状态以前部表为准。
- 正常观察：稳定raw状态2、run/output/fault为0、采样和任务计数增长，稳定区间错误不增加，耗时/内存值已记录。它只支持002停止状态复测，不等于所有保护、闭环或DMA通过。
- 异常：状态5、inhibit非0、输出意外1、计数停住或错误新增时，记录axis_status_word、board_fault_mask、board_first_fault、board_fault_ipsr；不修改变量绕过问题。初始化失败可在隔离供电下暂停查调用栈，该次数据另标为调试观察。
- 完成这轮后再实现003第6节的命令边界；未来DMA独立按003第9节选接口、内存和cache策略，并制定对应测试。不把新增命令或DMA与这次待机回归混作同一结果。

源码依据：`Core/Inc/axis_status.h:9–46`、`Core/Src/axis_status.c:5/12`、`Core/Src/app_runtime.c:169/217–225/318–347`、`Core/Src/freertos.c:143–183`、`Core/Src/ad2s1210.c:33/109–170/173–212/295–321/357–375`。原始实板数据和后续判定在下方或验证记录中追加，不覆盖本次NOT_RUN的历史状态。

### 8.6 首组4项截图复核（2026-11-06）

详见[002台架记录](../validation/002-axis-status-bench.md)。两张图axis_status_word均0x2，board_output_enabled均0；后台计数16918→29827（+12909），ADC消费计数338527→596800（+258273），增量比20.007204，与当前约20.008的设计关系相符。判为基本运行链初步检查通过；仅证明两个观察点待机、输出软件标志关闭，期间任务与采样有推进。

用户称时间约10秒和30秒，不作为精确频率测量；未展示的数据不补填，原有其余NOT_RUN项保持待测。此次截图不证明中间无瞬态异常、真实门极波形、完整闭环或DMA通过。

下一组仅看app_adc_missed_count、app_feedback_invalid_count、foc_isr_overrun_count，比较稳定区间前后增量；约30秒即可先做粗查。保持既定隔离供电与运行请求0，不修改计数或故障；通过后再少量检查耗时/内存。

### 8.7 三项异常计数补测与下一组（2026-11-06）

用户新截图：app_adc_missed_count=0，app_feedback_invalid_count=0x16（22），foc_isr_overrun_count=0，并报告“参数还是这样”。记录为本观察区间未报告新增异常，见[原始记录及截图](../validation/002-axis-status-bench.md)。本组只有一张新截图，没有独立T0/T1时间端点，不能写成精确Δ=0或60秒稳定性通过。22是反馈暂不可用的累计检查次数，不等于22次SPI通信故障，也不借001的22充当这组起点。

下一组只看：

| Watch | 看什么 | 判读 |
| --- | --- | --- |
| foc_isr_max_cycles | 控制处理函数历史最大耗时 | 当前200MHz下除200得到us；应低于源码现有9996cycles预算，停止路径不代表完整闭环 |
| app_stack_min_free_bytes | appTask历史最低剩余栈，字节 | 记录实值；总栈4096B，不用旧截图代填，不因有余量就缩栈 |
| app_heap_min_free_bytes | FreeRTOS堆历史最低剩余，字节 | 记录实值；总堆16384B，不代表整个芯片RAM |

保持当前隔离供电、运行请求0，不重启/清零；再自由运行约30秒后截这3项。大于0不能单独证明最坏内存场景安全；本轮先取得实际值，再决定是否需要其他参数。

### 8.8 第三组资源观察与阶段收尾（2026-11-06）

用户提供控制最大耗时及任务栈/堆最低余量截图，原值、换算及条件保存在[002台架记录第三组](../validation/002-axis-status-bench.md)。停止路径函数内耗时低于现有预算，任务栈与RTOS堆已记录可用余量；不把有剩余字节解释成所有最坏路径安全。

三组基础观察支持进入下一模块的纯软件命令边界开发。**本轮待机Watch检查到此收尾，不增加新参数作为软件开发前置条件。** 它只关闭停止状态的基础观察项，不关闭故障态、运行态、完整IRQ/临界区/MSP或真实输出验收。余项按未来功能需求分批验证，仍可在README找到回链。

下一独立功能围绕003第6节：请求整组交接、TIM1周期入口生效、明确接受/拒绝/应用、停止/故障/过期优先；先建立对应功能spec与纯软件测试，再接入运行链。不据本截图启用功率输出或宣称DMA可用。
