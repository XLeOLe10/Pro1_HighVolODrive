# 007 — CAN连通：FDCAN1与USB-CAN一问一答

**一句话：** 先让电脑与控制板完成一帧PING/PONG，验证真实CAN链路，再单独接入现有命令边界；本轮不发送电机控制命令。

**实施状态（2026-11-16）：** 已实现，低速连通检查点已达到、待用户提交。基于006提交`7fe5309`＋007工作区差异；SRC、HOST、BUILD通过。用户提供500 kbit/s、1000 ms周期的01/02请求与对应回复截图，以及四项异常计数全0截图，支持HW有限通过；不扩展为满负载、错误注入或带功率验收。用户确认更换接线后恢复，原接线波形异常，具体线序/接触问题未进一步判定。

| 部位 | 生成前 → 本轮实现 | 目的 |
| --- | --- | --- |
| 外设 | FDCAN未启用 → FDCAN1、PD0/PD1、经典CAN | 接入用户现有收发器 |
| 链路 | 只有USART速度发送 → USB-CAN请求与回复 | 先确认双向真实收发 |
| 调度 | 单业务任务 → 同一appTask中有界CAN服务 | 保留旋变与控制节拍 |
| 控制边界 | 自动命令演练 → 本轮保持原演练，CAN仅PING | 不引入第二个控制命令发布者 |
| 内存 | 无业务DMA → FDCAN专用Message RAM＋CPU工作副本 | 首轮不增加DMA池/cache维护 |

| 验收层级 | 状态 | 证据/限制 |
| --- | --- | --- |
| SRC生成差异 | PASS | FDCAN配置及原保护已核；补齐Bench遗漏的两份生成源，禁用空AfterMake动作；详见下文 |
| 新功能SRC | PASS | 有界轮询、DLC前护、单写、无电机控制副作用；独立复核完成 |
| BUILD | PASS | ARMCC5.06u7，两个目标`-b`均0错误0警告；[007原始记录](../validation/007-can-link-20261112.txt) |
| 编译期负向配置检查 | NOT_RUN | 本轮未重复非法宏组合测试；原禁输出编译约束保留 |
| HOST | PASS | 协议10869断言；服务23场景436断言；既有命令203＋FOC407断言，实际产品源参与；寄存器/HAL为模拟 |
| HW低速连通/有限调度观察 | PASS（有限） | 2026-11-16用户截图：01/02回显正确，周期约1 s；采样间隔、FOC函数体超时、任务超时、CAN错误计数均0；见第7节 |
| HW异常输入/负载与物理输出 | NOT_RUN | 非法帧/Bus-Off注入、满负载、最坏栈/IRQ及带功率未验收；不以本次正常收发替代 |

**下一步：** 1. 用户审阅本页前部和第7节，提交007低速连通检查点；2. 下一独立功能先定义CAN命令/回执，明确单一发布者、超时失效和禁输出测试，再接入现有命令边界；3. 满负载、错误注入及带功率验证继续单列，不因本次连通开放电机输出。

## 1. 已确认硬件与待核实项

- MCU为STM32H750VBT6/LQFP100，`FOC_T.ioc Mcu.CPN/Package`；用户确认ISO1042BDWVR与PD1/TX、PD0/RX。
- ST引脚表支持PD0=FDCAN1_RX、PD1=FDCAN1_TX；由CubeMX选对应复用。PB8/PB9仍归AD2S1210 SAMPLE/A1，不改接CAN。
- 用户模块资料标为UTC-T01；2026-11-16 PCAN-View显示PCAN-USB、500 kbit/s并有实际PING/PONG，证明当前安装组合可完成本轮经典CAN标准帧收发。具体固件/驱动版本、是否为原厂PEAK硬件及CAN FD能力未确认，不凭枚举名称推断。
- 工程`HSE_VALUE=25000000UL`，当前PLL及`.ioc`计算按25 MHz；同一行残留FPGA 60 MHz注释不作为硬件依据。位时序以实际HSE为25 MHz为前提，晶振/外部源实际频率仍需对照板图或测量。若不是25 MHz，先重算，不套用下表。
- ISO1042两侧供电分开：MCU逻辑侧按板图确认VCC1/GND1及电平匹配；总线侧VCC2/GND2要求4.5–5.5 V。用户此前报告VCC2对GND2为5 V，现有收发截图支持当前连接可通信；两端终端实际接入、适配器隔离结构和电气裕量仍未独立验收。带电电阻档读数不作为终端电阻依据。

初次预查记录：当时HAL_FDCAN未启用、驱动文件缺失且`.ioc`无FDCAN IP；2026-11-12用户已完成生成，现有`fdcan.c/.h`和HAL FDCAN驱动，HAL使能已打开。`APP_CAN_ENABLE`是“是否允许输出”的宏，与CAN总线无关。

## 2. CubeMX配置表（2026-11-12已生成）

只启用FDCAN1；主PLL、SYSCLK/HCLK、APB、TIM1/TIM7、ADC、SPI1、USART1时钟源/波特率和RTOS配置保持当前值。电源档位明确由旧VOS3修正为VOS1，理由见下段。初始`.ioc`的`RCC.FDCANFreq_Value=200000000`不沿用，当前CAN已改选HSE 25 MHz直供。

| 页面/字段 | 本轮设置 |
| --- | --- |
| Power Regulator Voltage Scale | Scale 1（修正旧档位与总线频率不匹配） |
| SYSCLK / HCLK / APB | 保持200 / 200 / 100 MHz；主PLL和总线分频不改 |
| USART1/6 Clock Mux | 保持PCLK2=100 MHz；USART1仍115200、8N1 |
| Connectivity / FDCAN1 | 启用，Normal模式 |
| 引脚 | PD0=FDCAN1_RX，PD1=FDCAN1_TX；不使用PB8/PB9 |
| Frame Format | Classic CAN；不开FD/BRS |
| Clock Configuration / FDCAN时钟源 | HSE；显示25 MHz（实际外部源须一致） |
| Clock divider（若有） | DIV1 |
| Nominal Prescaler | 5 |
| Nominal Time Seg1 | 7 TQ |
| Nominal Time Seg2 | 2 TQ |
| Nominal Sync Jump Width | 1 TQ |
| 计算结果 | 25 MHz / [5 × (1+7+2)] = 500 kbit/s；采样点80% |
| Data timing（经典CAN不用） | 若界面要求合法值，Prescaler=5、Seg1=7、Seg2=2、SJW=1；不启用BRS |
| Auto Retransmission | Disable，首轮单次发送，便于发现缺ACK/错误接线；后续可靠性另定 |
| Transmit Pause / Protocol Exception | Disable / Disable |
| Message RAM Offset | 0，仅FDCAN1使用 |
| Standard Filters / Extended Filters | 1 / 0 |
| Rx FIFO0 Elements / Element Size | 8 / 8 bytes |
| Rx FIFO1 / Rx Buffers | 0 / 0（相应元素大小若必填，8 bytes） |
| Tx Buffers / Tx FIFO Queue Elements | 0 / 32（适配CubeMX 6.16.0校验，见下文） |
| Tx FIFO Queue Mode / Element Size | FIFO / 8 bytes |
| Tx Event FIFO Elements | 0，首轮以电脑收到PONG为真实发送证据 |
| NVIC FDCAN1 IT0/IT1 | 初轮均不启用；appTask有界轮询 |
| DMA | 不启用 |

2026-11-11时钟约束修正：之前仅因VOS1偏离旧基线而建议恢复VOS3，未完整检查总线限值，此建议撤回。ST DS12556 Rev8表110（p200，rev V）列明VOS3的CPU/HCLK/APB上限分别为200/100/50 MHz，VOS1为400/200/100 MHz。当前源码和工程使用SYSCLK=200、HCLK=200、APB=100 MHz；旧VOS3组合至少在HCLK/APB上不满足该规格，不能因以前能运行就视为合规。现保留VOS1，不再为了复原旧值修改串口mux或主PLL。此时USART1/6仍取PCLK2 100 MHz，FDCAN仍取HSE 25 MHz。芯片实际修订号未板端回读，生成后仍核对适用的Flash等待周期及VOS就绪流程；VOS1运行功耗/热条件和新固件行为未板测。来源：[ST数据手册表110](https://www.st.com/resource/en/datasheet/stm32h750vb.pdf#page=200)。该段为生成前审查历史；2026-11-12源码已使用VOS1及VOSRDY等待，Flash改为LATENCY_2。官方RM0433 Rev9表17（p160）给出VOS1/AXI200 MHz对应2 WS，表60（p356）给出VOS1下USART1/6内核上限100 MHz。Flash编程用WRHIGHFREQ是另一项，本轮未实现在线Flash写入，不把读等待匹配视为编程验证。

2026-11-11配置修订：当前工程为CubeMX 6.16.0，用户截图提示Tx Buffers与Tx FIFO数量之和必须为32。ST官方确认正确限制应为不超过32，界面问题编号222211，见[ST答复](https://community.st.com/stm32cubemx-mcus-29/why-in-cubemx-the-sum-of-the-two-parameters-must-be-be-32-160260)。原0+3满足硬件限制，但此界面拒绝；本轮改用0+32，避免改版本或手工绕过生成器。只扩大发送FIFO容量，不改变帧长、500 kbit/s或每任务轮最多1帧的处理预算。

按1个标准过滤器、8个RX元素、32个TX元素及8字节载荷估算，Message RAM为1+8×4+32×4=161个32位字，即644 B，比原方案增加464 B；生成HAL布局已核：起点`SRAMCAN_BASE=0x4000AC00`，结束（不含）`0x4000AE84`；`CanLink_Init`核对RX首地址和总644 B边界。若总线受阻，FIFO可积压更多旧PING回复，继续以对端收到匹配序号为证据，不把成功入队当作送达；未来FDCAN2需重新划分该共享RAM。该数量变更已生成；源/构建/板测状态按本页验收表区分。

HAL中NominalPrescaler/TimeSeg/SJW应为上表实际计数，不自行按寄存器编码减1。只按界面已有项填写；不同CubeMX版本字段名可能有差别，保存后按实际`.ioc`复核。

生成差异检查：TIM1/ADC/SPI/USART/RTOS/IRQ/board_safety代码未被覆盖，EN低初值保留，GPIO仅新增GPIOD时钟。CubeMX只给普通目标加了`fdcan.c`和HAL驱动，Bench缺失已补齐；两个目标原宏/独立产物保留，空AfterMake动作还原，工程XML恢复原LF换行以便审阅。正常目标只进行MX外设初始化，不调用`CanLink_Init/Poll`或启动CAN，也不开放START。

## 3. 软件职责、数据所有权及错误处理

- 首轮CAN服务仅在强制禁输出的测试目标启用；不新建业务任务、不调用AxisCommandPort_Submit/Stop、不改变活动命令、PWM或EN。
- appTask先完成旋变采集，再每轮最多处理1个接收帧、尝试1次回复；不用while排空持续流量，不等待TX空位或完成。初轮电脑发送频率不超过10帧/秒，仅作低负载链路验收。
- 外设独占接收写入Message RAM的RX FIFO0；CPU/appTask用HAL读取完整帧到自己的固定工作副本。发送端appTask准备完整8字节回复，HAL复制到TX Message RAM并提交；提交成功仅代表入队，不等于已被对端收到。
- 普通工作副本仅CPU使用，按当前链接放AXI SRAM的静态区或appTask栈；没有DMA1/2访问，故不新增DMA cache维护。FDCAN Message RAM属于外设专用地址空间，按HAL及器件映射访问；生成后的分配和边界仍须核对，不能与未来FDCAN2重叠。
- 初轮不启用FDCAN中断，不存在CAN ISR与appTask争抢该工作副本。调试计数由appTask单写；若以后加状态回包，必须明确一致快照，不能拼接多个独立volatile量后声称同一时刻状态。
- 检查FIFO丢失、总线错误、Bus-Off及TX忙；在轮询路径读取并按手册清理相应状态，不依赖未启用的IRQ回调。记录可区分原因的计数/状态，不无限重发、不阻塞采样。Bus-Off本轮不自动恢复；恢复流程后续明确，不由CAN恢复触发电机启动。
- 实施时审核HAL热路径的循环上限；Start/Stop/初始化不得混入周期处理热路径。后续若吞吐要求超出首轮轮询预算，再单独评估中断/有界队列，不为本次低速验证提前增加DMA。
- 接入真实START/UPDATE/STOP是后续独立步骤：CAN命令生产者必须与现有Bench自动生产者互斥，保留唯一普通发布来源、命令期限/代次，以及接受/生效的不同回执含义。

## 4. 最小测试协议与验收

在独立的电脑—控制板两节点台架上使用临时测试ID，不声明兼容ODrive CANSimple、CANopen或任何已有总线协议。

| 方向 | 标准ID | DLC | 载荷 |
| --- | --- | --- | --- |
| 电脑→板子 | 0x321 | 8 | `50 49 4E 47 ss ss ss ss`，ASCII PING，后4字节为原样回显序号 |
| 板子→电脑 | 0x322 | 8 | `50 4F 4E 47 ss ss ss ss`，ASCII PONG，同序号 |

过滤只接收0x321标准数据帧到FIFO0；全局过滤拒绝其他标准/扩展ID及远程帧。软件再次检查ID、帧类型、经典格式、DLC=8和PING标记；非法输入不回显、不执行命令。协议实现及过滤器是生成后手写部分，不把配置Filter数量当作过滤器已设置。

验收顺序：静态/主机验证解析边界和无控制副作用 → 两目标增量构建 → 母线断开、控制/传感器及CAN两侧供电下，USB-CAN正常模式500 kbit/s，低速请求与对应回复 → 独立一小轮观察采样/调度是否受影响。USB-CAN必须参与ACK，纯监听模式不能代替收发节点；逻辑分析仪不提供CAN ACK。

第一轮最多3个Watch：`can_link_rx_count`（合法PING数量）、`can_link_error_count`（观察到的错误事件次数）及现有`board_output_enabled`；前两变量仅在Bench目标链接，以电脑实际收到带匹配序号的PONG为回复证据。下一轮才看既有采样间隔/任务超时指标，不一次堆参数。

## 5. 接线与风险边界

- 功率母线继续断开。USB-CAN CANH→板CANH，CANL→板CANL；按收发器/适配器说明使用总线参考地，ISO1042区分GND1与GND2，不用逻辑分析仪地夹把隔离两侧短接。
- 总线两端各一处120Ω终端，先查板上和适配器已有终端，避免重复并联。关电后完整总线CANH/CANL间通常约60Ω，仅作终端检查，不能证明通信链路通过。
- 逻辑分析仪先看MCU侧PD1/TXD、PD0/RXD及GND1，需核对输入耐压/逻辑阈值；不直接把未知规格探头接CANH/CANL。物理总线波形质量需要合适的差分测量手段，普通数字解码不等于电气质量验收。
- USB-CAN是否隔离尚未知；若它与ST-Link/逻辑分析仪共用电脑，USB地可能旁路连接GND1/GND2。板上有ISO1042不等于整套调试链隔离，须核对设备地连接；测试继续保持功率母线断开。
- 关键风险：外部HSE与软件25MHz假设不符；隔离总线侧没供电；USB-CAN固件/驱动不匹配或处于监听；终端/接线错误；过量帧拖慢appTask；CubeMX覆盖手写防护。首轮不包含满总线负载、DMA、真实电机命令或带功率验证。

依据：现有[架构](../ARCHITECTURE.md)与[003后续通信/DMA合同](003-c-port-architecture.md)；[ST STM32H750数据手册](https://www.st.com/resource/en/datasheet/stm32h750vb.pdf)的FDCAN/引脚章节；[ST AN5348](https://www.st.com/resource/en/application_note/an5348-fdcan-peripheral-on-stm32-devices-stmicroelectronics.pdf)的专用Message RAM/帧格式；[TI ISO1042数据手册](https://www.ti.com/lit/ds/symlink/iso1042.pdf)的两侧供电与终端要求。


## 6. 实际代码与边界（2026-11-12）

| 文件/函数 | 实际职责与边界 |
| --- | --- |
| `Core/Src/fdcan.c MX_FDCAN1_Init/HAL_FDCAN_MspInit` | CubeMX外设、HSE源和PD0/1 AF9初始化；不开IRQ、不Start |
| `Core/Src/can_link.c CanLink_Init` | appTask启动时只尝试一次；校验RAM布局/配置/中断关闭，标准ID321掩码7FF、全局拒绝不匹配与远程、FIFO0 BLOCKING，然后Start；失败留诊断且不重试 |
| `Core/Src/can_link.c CanLink_Poll/PollErrors` | 每轮最多取1帧、尝试入队1帧；无等待、分配、关中断、打印或电机命令；BUS_OFF/RAM错误锁停服务，不自动恢复 |
| `Core/Src/can_link_protocol.c CanLinkProtocol_MakeReply` | 独立纯C验证完整元数据与PING magic，成功才赋值PONG；拒绝保持输出不变，支持原地调用 |
| `Core/Src/app_runtime.c AppRuntime_TaskStart/BackgroundStep` | 仅Bench启用；初始化在CurrentSense_Start之前；周期服务在AD2S采集之后；原Bench自动命令生产者不变 |
| `MDK-ARM/FOC_T.uvprojx` | 两目标都有新增源文件；普通目标服务编译分支为空且无调用；Bench仍强制BOARD_POWER_STAGE_MODE=0 |

接收长度前护：本地FW H7 V1.12.1驱动`HAL_FDCAN_GetRxMessage`按原始DLC表拷贝，未按配置槽长截断；因此在HAL前用当前FIFO GetIndex读取word1的DLC，只允许8，其他值只写RXF0A确认丢弃。显式BLOCKING、唯一消费者、GetIndex<8及元素大小/地址校验保证ACK前该槽不会被覆盖；合法帧才让HAL复制8 B，再复查格式、ID和magic。未修改ST HAL源码，也不用大缓冲掩盖跨槽读取。

数据所有权：FDCAN硬件填RX专用RAM；appTask消费完整帧到栈上CanLinkFrame并完全构造回复，HAL复制到TX专用RAM后CPU局部副本即可复用。没有DMA1/2或CPU数据缓存共享池；Message RAM为外设Device区域，不做D-cache clean/invalidate。TIM1不读写CAN缓冲，CAN不读写控制命令/PWM/EN。新服务占用的任务栈需本轮板测复核，静态调用深度不是最坏栈证明。

错误语义：IR按已读标志写1清除，区分RX丢失/协议错误/RAM问题/Bus-Off；PSR每次只由服务读一次，ECR出错时留诊断。`can_link_error_count`是轮询观察事件数，标志合并可使它少于真实错误次数；非法协议帧只增加`can_link_diag.rejected_count`，TX满丢本次回复并记忙/错误，不保存软件重试队列。`tx_enqueued_count`不表示送达。诊断结构各成员不是同一时刻原子快照，不提供控制许可。

复现用板测步骤（2026-11-12拟定，2026-11-16已取得的结果见第7节；未执行项不自动通过）：
1. 确认使用已重新载入源清单的FOC_T_CommandBench目标，F7 Build后按用户操作下载；母线保持断开，确认ISO1042总线侧5 V存在。
2. 软件识别适配器后设Classic CAN、500 kbit/s、Normal/Active；先手动单发标准数据帧ID321、DLC8、`50 49 4E 47 01 00 00 00`。
3. 期待收到ID322、DLC8、`50 4F 4E 47 01 00 00 00`；上述三个Watch期待合法接收数增加、错误0、输出0。没有回包先定位，不加功率。
4. 单帧通过后再做低速连续与错误帧、采样/调度小轮验证；初轮≤10帧/秒，不据此声称满总线负载通过。硬件结果由用户提供后追加证据。


验证收尾：两目标增量编译分别3分29秒（Bench）、3分19秒（普通），公共HAL头变更使依赖文件重编，不是调用Rebuild All。Bench实际ROM 65272 B、RW+ZI 25976 B；正常目标数值见原始记录。完整日志、源/产物SHA256及静态栈摘录见[007证据](../validation/007-can-link-20261112.txt)。上述为2026-11-12软件验证时点，代理未进行下载、硬件操作或Git暂存/提交；后续用户板测结果见第7节。2026-11-13又复跑HOST并对Bench执行增量Build，0错误0警告、1秒，AXF/HEX未变化；2026-11-16仅整理证据，不重复编译。


## 7. 低速连通检查点收尾（2026-11-16）

测试沿用FOC_T_CommandBench、功率母线断开、控制/传感器及CAN供电的台架约束；板上镜像没有独立回读指纹。截图与用户陈述支持本轮有限结论，不冒充完整自动化板测日志。

| 已观察到的结果 | 证据 |
| --- | --- |
| 标准ID321/DLC8的PING 01得到ID322/DLC8的PONG 01；PCAN为500 kbit/s，发送1000 ms、接收显示999.5 ms | [序号01原图](../validation/007-can-link-20261116-seq01.png) |
| 将末4字节改为02 00 00 00后，回复也变为02 00 00 00；接收显示999.8 ms | [序号02原图](../validation/007-can-link-20261116-seq02.png) |
| ad2s_sample_gap_count、foc_isr_overrun_count、app_task_deadline_misses、can_link_error_count均为0；用户确认“没问题正常发然后正常can通信” | [四项Watch原图](../validation/007-can-link-20261116-health.png) |
| 用户报告“换了接线，之前接线测得波形有问题”，随后恢复收发 | [原始记录末尾追加](../validation/007-can-link-20261112.txt)；不能据此断言旧USB-CAN或固件损坏 |

本轮建议观察约30秒，但用户未提供独立起止时间或成对起止计数；两张PCAN图的累计计数包含先前测试，两图之间有编辑操作且缺少统一起止窗口，因此不计算跨图丢包率，不记“精确30秒零丢帧”。四项Watch为结束截图，支持该次观察中未记录相应异常；不代表所有时间、所有负载都无异常。新截图未包含board_output_enabled或CAN error_flags，不补造这些读数。

结论：可提交“低速CAN PING/PONG连通＋有限调度观察”的检查点。异常输入/总线错误注入仅有HOST证据；新CAN负载下最坏栈、完整IRQ延迟、实际输出/保护、电气质量、长期稳定性及精确收发统计仍不在本次通过范围内。
