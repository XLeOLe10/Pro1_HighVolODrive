# 008 — CAN命令：ODrive对照与接入方案

**一句话：** STOP已提交；本轮加入同一帧里的速度/Iq整组参数预检和回显，通过仅表示范围合格，不授予启动许可。

**实施状态（2026-11-17）：008.03预检已实现，SRC/HOST/BUILD通过，正常低频禁输出HW PASS（有限，用户反馈见9.7）。** 基线`Xiao_ODrive / 98f736d`＋本轮工作区差异。008.01查询、008.02 STOP及其历史板测在第6–8节；本轮看第9节。会话与报文新鲜度尚未闭合，因此没有把预检直接接到START/UPDATE；范围调整原因与后续合同见9.3。

| 部位 | 本轮实际变化 | 边界 |
| --- | --- | --- |
| 线路字段 | 单帧速度signed16 rpm＋Iq unsigned16 mA | 字段成对解码，不拆包、不偷偷限幅 |
| 参数检查 | 从既有命令规则抽取共享ValidateTargets | 复用同一静态限值，不复制一套阈值 |
| CAN业务 | 增加预检/原参数回显 | 重复只重验，不提交/续期或缓存运行命令 |
| 控制写入 | STOP仍是唯一CAN控制请求 | 自动Bench仍停用，START/UPDATE未接入 |
| 文件组织 | 9个已有固件文件＋5个已有测试文件 | 新增源/头文件0，沿用008文档和证据 |

| 验收层级 | 当前状态 | 依据/限制 |
| --- | --- | --- |
| SRC | PASS | 独立复核参数校验等价、整组/字节转换、无控制副作用、上下文错误映射 |
| BUILD | PASS | 两个目标增量0错误0警告；原始日志/最终指纹追加到008记录 |
| HOST协议/服务 | PASS | 70347断言；58场景2475断言，含STOP/预检交错 |
| HOST核心/端口/安全 | PASS | core203＋FOC407；端口/演练/安全431断言＋9配置组合 |
| HW 008.03预检 | PASS（有限） | 用户确认BUILD008.03、合法/越界结果01/02及三项Watch为0；无新回包截图/板端镜像hash回读，见9.7 |
| HW历史STOP | PASS（有限） | 第8.6–8.7节；已提交98f736d，不含物理停车/最坏时序 |

**下一步：** 1. 用户提交008.03检查点（固件9、测试5、文档/证据4）；2. 进入会话/新鲜度/结果保留合同；3. 在禁输出目标接入并验收START/UPDATE。原始日志见[008记录](../validation/008-can-query-20261116.txt)，当前分工见[架构](../ARCHITECTURE.md)。

## 1. 参考版本与协议含义

仅比较 **ODrive fw-v0.5.6，commit `a308314ed2ca613164b81e7bbdfacc53cd1859ff`**，不混用新版Pro/S1的寻址、SDO或固件更新命令。

- [命令枚举](https://github.com/odriverobotics/ODrive/blob/a308314ed2ca613164b81e7bbdfacc53cd1859ff/Firmware/communication/can/can_simple.hpp)
- [分发与处理源码](https://github.com/odriverobotics/ODrive/blob/a308314ed2ca613164b81e7bbdfacc53cd1859ff/Firmware/communication/can/can_simple.cpp)
- [该版本协议说明](https://github.com/odriverobotics/ODrive/blob/a308314ed2ca613164b81e7bbdfacc53cd1859ff/docs/can-protocol.rst)与[命令字段CSV](https://github.com/odriverobotics/ODrive/blob/a308314ed2ca613164b81e7bbdfacc53cd1859ff/docs/figures/can-protocol.csv)
- [接口单位定义](https://github.com/odriverobotics/ODrive/blob/a308314ed2ca613164b81e7bbdfacc53cd1859ff/Firmware/odrive-interface.yaml)

表中`0x001`等是**命令号**。标准帧完整ID为`(node_id << 5) | command_id`，该版本节点0～63，多字节小端。现有PCAN中的`0x321/0x322`则是本项目完整测试ID；按CANSimple解释会变成节点25的Heartbeat/Estop，因此它们不兼容ODrive，不应混用于真实ODrive总线。若选择兼容方案，测试ID须迁移或隔离。

纯C与CANSimple兼容不冲突：一个是实现语言，一个是外部协议。兼容必须逐项匹配ID、DLC、字段、单位、状态与响应规则；不能因为函数名称接近就宣称兼容。本轮已选择自定义只读诊断协议v1；后续是否增加CANSimple兼容层仍独立讨论，不默认兼容。

## 2. ODrive命令与本项目取舍

本表覆盖该版本头文件命令目录，保留整体功能取舍。PING/PONG、四个查询、第8节STOP及第9节整组参数预检已实现；其它控制、反馈和配置相关项目仍是后续建议。

| ODrive命令号 / 名称 | 原版用途 / 实现差异 | 本项目对应与建议 |
| --- | --- | --- |
| 0x001 Heartbeat | 主动发送轴状态、故障与轨迹完成等 | 先GET_STATUS；低频主动状态后续加入，不能给运行命令续期 |
| 0x002 Estop | 设置轴ESTOP错误；回调本身不直接写PWM关断 | 先STOP撤许可；锁存急停另定，不宣称等价 |
| 0x003 Get Motor Error | 读电机故障uint64 | GET_STATUS包含board_fault_mask；不伪装为ODrive位定义 |
| 0x004 Get Encoder Error | 读编码器故障uint32 | 后续细分旋变诊断，不能直接复用故障位 |
| 0x005 Get Sensorless Error | 读无感估计器故障 | 暂不做，本轮使用AD2S1210 |
| 0x006 Set Axis Node ID | 设置轴节点号 | 首版固定身份；多节点/持久化另立功能 |
| 0x007 Set Axis Requested State | 请求IDLE=1、闭环=8等状态 | START_SPEED/STOP表达最小启停；当前没有完整Axis生命周期 |
| 0x008 Set Axis Startup Config | 函数体Not Implemented | 不移植空入口，不默认上电运行 |
| 0x009 Get Encoder Estimates | 位置turn、速度turn/s | GET_FEEDBACK先速度rpm/角度有效性；位置坐标未定不冒充多圈机械位置 |
| 0x00A Get Encoder Count | 线性累计/CPR内计数 | 暂不做，旋变原始码不等于编码器多圈计数 |
| 0x00B Set Controller Modes | 控制模式与输入模式 | 首版固定速度模式；其它模式明确拒绝 |
| 0x00C Set Input Pos | 位置+速度/力矩前馈 | 位置环和位置模式暂不做 |
| 0x00D Set Input Vel | 速度目标+力矩前馈 | START_SPEED/UPDATE_SPEED整组速度rpm+Iq上限A，不直接照搬载荷 |
| 0x00E Set Input Torque | 力矩目标N·m | 暂不做；缺Kt等参数不能把N·m当A |
| 0x00F Set Limits | 速度上限+电机电流上限 | 本地确认的硬上限＋每组命令Iq约束；不是原版“速度目标”命令 |
| 0x010 Start Anticogging | 已调用抗齿槽标定入口，不是空实现 | 暂不做，不影响首版速度命令链 |
| 0x011 Set Traj Vel Limit | 轨迹速度限制 | 暂不做轨迹规划 |
| 0x012 Set Traj Accel Limits | 轨迹加/减速度限制 | 暂不做，现有速度斜坡不等于完整轨迹规划器 |
| 0x013 Set Traj Inertia | 轨迹惯量前馈系数 | 暂不做 |
| 0x014 Get Iq | Iq给定与实测 | GET_FEEDBACK的后续字段；需同一快照及有效性 |
| 0x015 Get Sensorless Estimates | 无感位置rad、速度turn/s | 暂不做；这里的位置单位例外，不能统一写turn |
| 0x016 Reboot ODrive | 直接NVIC_SystemReset，整MCU重启 | 暂不开放；重启不是正常停车命令 |
| 0x017 Get Bus Voltage and Current | 母线电压/电流 | 先提供已有母线电压；无母线电流测量/估计合同，不以Iq代替 |
| 0x018 Clear Errors | 调odrv.clear_errors，整板清错 | 暂不开放；本工程无运行时清故障API |
| 0x019 Set Linear Count | 修改编码器坐标计数 | 暂不开放；坐标/校准单独设计 |
| 0x01A Set Position Gain | 位置环增益 | 暂不做位置环配置 |
| 0x01B Set Vel Gains | 速度PI增益 | 后续独立配置事务；不在运行中直接写共享PI |
| 0x01C Get ADC Voltage | 请求带GPIO号，回复电压 | 不照搬通用GPIO查询；需要时只暴露明确测量通道 |
| 0x01D Get Controller Error | 读控制器故障uint32 | GET_STATUS汇总本地故障与拒绝原因，位定义分开 |
| 0x000 CANopen NMT | 保留，分发跳过/回调未实现 | 不实现CANopen |
| 0x700 CANopen Heartbeat | CANopen保留项，不是低5位普通命令 | 不实现CANopen |

`0x01E/0x01F`未分配；该版本没有独立Save Configuration CANSimple命令，也没有名为START/UPDATE/STOP的三条业务命令。

几个必须保留的差别：

1. **单位：** ODrive普通位置turn、速度turn/s；我们的速度rpm需要×/÷60。力矩N·m与Iq电流A必须经明确电机参数换算。轨迹惯量单位N·m/(turn/s²)，也不能直接套kg·m²数值。
2. **回执：** 多数Getter接受RTR或DLC=0请求、用相同ID回复；Get ADC Voltage要携带GPIO号，Get Controller Error分发检查不同。原版写命令没有统一的接受/生效回执。我们的结果合同是额外设计，不是已移植功能。
3. **续期：** 原版do_command在switch前watchdog_feed；不照搬“查询也喂狗”。建议只有新鲜且合法、按合同接受的控制更新影响运行期限；PING、状态查询、重复/非法帧都不能延长授权。
4. **原子性：** 原版Set Input Vel与Set Limits分别写字段，不能据此认为两帧构成一笔事务；本工程继续要求整组目标/限制一次交给TIM1。
5. **安全：** 软件STOP、锁存故障、硬件急停是不同职责。现有STOP是撤EN/MOE后的停机，不是受控减速；Break仍未启用，物理停机延迟未验收。

## 3. 拟议的最小命令集与当前基础

像送一张工单：CAN负责递单，命令层检查内容，TIM1在固定工作节拍接单。收到工单、开始照工单执行、电机已经达到目标，是三件事。

| 建议逻辑命令 | 读写内容 / 预期结果 | 已有基础与缺口 |
| --- | --- | --- |
| PING/PONG | 原样回显测试数据 | 已完成007；若选CANSimple需处理ID冲突 |
| GET_INFO / GET_BUILD | 能力/结构版本、手动软件接口修订号 | 已实现；不是固件hash。模式/核心配置有效位见STATUS，真实运行上限未开放 |
| GET_STATUS / GET_FAULT | 观测阶段、许可镜像、输出软件标志、禁止原因/故障mask | 已实现第6节独立查询；不是同拍快照。具体命令拒绝结果仍待后续 |
| GET_FEEDBACK | 先速度、母线电压及有效性/数据年龄，Iq等按需分组 | 有测量基础；需新快照/单位/分帧合同，数据读取不额外做SPI |
| STOP | 不受未确认运行配置或普通邮箱满限制地请求撤许可；返回停止票据和处理结果 | 已有AxisCommandPort_Stop；不是普通START/UPDATE槽，也不是即时总线硬急停 |
| START_SPEED | 明确开始新一轮速度运行：目标rpm、Iq上限A、期限/身份 | 内部START已有；CAN解码、会话/序号关联未实现 |
| UPDATE_SPEED | 在当前有效运行内整组更新目标/限制 | 内部UPDATE已有；不能从停机、到期或旧epoch恢复运行 |
| GET_COMMAND_RESULT / 回执 | 关联请求：拒绝、接受、应用、取消、停止处理 | 已有内部序号/一致快照；可靠结果保留与重读语义未实现 |

GET_INFO可报告已确认的运行上限；暂不另增一个运行时写限值命令。第一阶段只读，第二阶段先STOP再START/UPDATE，全部在禁输出目标验收。板上Watch按轮选3～4项，字段确定后再给测试清单。

当前源码定位：

| 内容 | 文件与函数 |
| --- | --- |
| 启动/任务创建 | [main.c](../../Core/Src/main.c) main；[freertos.c](../../Core/Src/freertos.c) MX_FREERTOS_Init / StartAppTask |
| 后台实际顺序 | [app_runtime.c](../../Core/Src/app_runtime.c) AppRuntime_BackgroundStep：AD2S → CanLink_Poll（含STOP）→ 遥测/堆统计；不再调自动Bench |
| 当前线路协议 | [can_link_protocol.c](../../Core/Src/can_link_protocol.c) CanLinkProtocol_MakeReply；[can_link.c](../../Core/Src/can_link.c) CanLink_Init/Poll |
| 内部命令与STOP | [axis_command.c](../../Core/Src/axis_command.c) AxisCommandPort_Submit / Stop / BeginCycle / FinalPermit / GetSnapshot |
| 纯C规则 | [axis_command_core.c](../../Core/Src/axis_command_core.c) AxisCommand_Validate / PublishValidated / Tick / Guard；[数据类型](../../Core/Inc/axis_command_core.h) |
| 历史自动请求源（已停用） | [axis_command_bench.c](../../Core/Src/axis_command_bench.c) AxisCommandBench_Init/Step；保留源/HOST，不再调度 |
| FOC/最终输出 | [app_runtime.c](../../Core/Src/app_runtime.c) AppRuntime_FastISR；[open_foc.c](../../Core/Src/open_foc.c) OpenFOC_ApplyCommand/Run_20kHz；[board_safety.c](../../Core/Src/board_safety.c) SetOutputsEnabled/LatchFault |

普通目标command_limits仍未确认，拒绝START/UPDATE；Bench的1000rpm、4A、1000ms只是合成夹具，不能作为电机参数。Bench始终ApplyCommand(NULL)，输出禁止。CAN目前只在Bench启动；仅STOP可发控制请求，整组参数预检不会发控制请求，不等于普通目标已有通信命令。

## 4. 拟议接入图与必须先定的合同

本图保留完整控制阶段方案，STOP最小子集已按第8节落地；现有完整结构图只维护在[架构页](../ARCHITECTURE.md#启动与执行上下文)。

```mermaid
flowchart TD
    PC["电脑 PCAN"] --> CAN["既有 FDCAN 有界轮询"]
    CAN -. "拟增加" .-> DEC["纯C解码 / 格式与身份校验"]
    DEC --> READ["只读查询 / 完整遥测快照"]
    DEC --> CMD["整组控制事务校验"]
    CMD --> SUB["既有 Submit / 独立 Stop"]
    SUB --> ISR["既有 TIM1 命令消费边界"]
    ISR --> RES["生效命令号和控制拍号 / STOP已处理票据"]
    RES -. "拟增加结果保留" .-> ACK["回执 / 结果重读"]
    SUB --> ACC["任务侧：接受序号或拒绝 / STOP请求票据"]
    ACC --> ACK
    READ --> ACK
    ACK --> PC
    AUTO["现有自动 Bench 生产者"] -. "与CAN控制源互斥" .-> SUB
    ISR --> LIMIT["Bench继续强制空命令 / 禁功率输出"]
```

### 4.1 一次命令怎样完整交接

- 经典CAN一帧最多8B，内部AxisCommandFrame为32B，不能直接memcpy当线路协议；字节序、浮点表示、单位、非法值和范围要逐字段定义。
- **方案A：紧凑单帧。** 两个float32恰好8B，但序号/会话/期限不再有空间，必须说明哪些来自ID、哪些由板端配置/会话补齐；不能只凑下数值就宣称防重放已解决。
- **方案B：多帧事务。** 静态有界暂存、事务号、齐包标志、首帧接收时间与超时、重复/乱序规则全部明确；未齐不得发布，新旧片段不得混合。STOP使在组装的启动事务失效。
- appTask持有稳定局部副本，校验后经现有Submit的短PRIMASK整组发布；TIM1在下一次符合条件的控制边界消费，输出前再复核。不能用volatile或只屏蔽低优先级IRQ的RTOS临界区替代。
- 若接收队列产生滞留，必须定义“接收时间”是硬件入队还是软件出队，并给排队年龄上限；不能给排队很久的启动帧重盖一个新时间戳。跨复位身份/旧命令清理也要单列，当前epoch只解决一次启动内的旧代次。

### 4.2 谁能发、何时续期、何时停止

- 应用层只有appTask一个普通发布者；选择CAN控制源时关闭自动Bench脚本的发单行为，保留独立回归测试入口。两者碰巧在同一任务也不代表业务上可以同时控制。
- 查询不授予permit，心跳不续运行期限。没有有效更新时现有到期逻辑撤许可，不自动恢复；恢复需要新一轮合法START。
- STOP独立于普通命令邮箱，但**仍受CAN仲裁、硬件FIFO、后台调度影响**。当前FIFO0每轮最多处理1帧，不能声称STOP已绕过队列。实现前选定保留接收资源/优先服务或其它有界策略，并验证最坏延迟及Bus-Off处理；不凭设计数字承诺安全急停时间。
- 满队列、失联/Bus-Off、非法帧的处理要有界。协议故障是否主动请求STOP与控制命令超时的关系需写明；目前007只锁停CAN服务，不负责电机许可。
- 同一任务还有阻塞SPI，故障时超时可影响通信响应；当前低速正常测试不证明所有故障条件下任务互不影响。

### 4.3 回执和遥测的可信度

- `ACCEPTED`仅代表完整工单进入邮箱；`APPLIED`表示指定command_seq已在某个control_seq被控制端接收；Bench中也不代表FOC带输出运行，更不代表实际速度达到目标。
- 现有72B GetSnapshot是当前状态快照，不是事件队列。新协议需固定容量结果保留/重读规则、请求号与内部seq/STOP票据映射，明确覆盖/忙/重试行为。发送入队不等于电脑收到。
- 重复请求不能重复启动或续期；请求已处理时怎样返回同一结果、会话失效后怎样拒绝，要与报文一起定稿。
- 状态/反馈每组有快照号、时间/有效性；跨多帧只拼同一快照。没有可证明的母线电流或多圈位置不提供假值，缺测要标无效/不支持。

## 5. 设计选择与验收顺序

| 选择 | 适用场景 | 代价 |
| --- | --- | --- |
| 自定义最小协议（本次建议，未定稿） | 先用PCAN建立可解释的命令/回执，满足整组更新与禁输出演练 | 不直接兼容ODrive工具，后续需自有上位机或明确适配层 |
| CANSimple兼容子集 | 已确定要复用支持该旧版本的上位机 | 精确遵守ID/单位/查询方式；原版多条设置命令与本项目原子事务的关系必须另设计，不可悄悄改变语义 |

“参考ODrive机制”和“对外CANSimple兼容”分别决定。第6节已固定自定义查询、第8节已固定STOP；本表保留后续速度控制协议的取舍，完整运行事务和CANSimple兼容层仍未实现。

1. 固定协议选择、版本、能力位、字段、非法输入规则；源码静态检查不得出现通信直接写PWM/EN/FOC活动变量。
2. 只读查询：HOST覆盖DLC/ID/单位/边界，两个目标增量Build，禁输出板上核版本和状态；新接口先少量低频发送。
3. 控制接入：先验证STOP，再整组START/UPDATE、忙/重复/过期/旧会话、取消结果；保持Bench强制禁输出，接入后重测调度与内存。
4. 再做有界负载、接收拥塞/回执丢失/失联故障测试。真实电机参数、采样对时、物理保护与带输出闭环另验收，本功能通过不自动开放功率。

后续控制接入风险：不要把只读查询通过误当作电机命令已就绪，也不直接照搬ODrive单位、回执或watchdog语义。控制事务需先定稿，再实现。

## 6. 第一步只读查询合同（008.01历史版本，2026-11-16）

本节保留已提交`488d481`的第一步合同与证据；008.02变更及当前操作看第8节。首轮采用自定义诊断协议v1，保留007的321/322及PING/PONG；不宣称CANSimple兼容。只做INFO/STATUS/FAULT/BUILD查询，反馈与START/UPDATE/STOP线路命令仍未开放。自动Bench仍是唯一控制请求来源，尚不切换生产者。

### 6.1 帧与返回值

全部标准数据帧、经典CAN、DLC=8、500 kbit/s；请求ID=0x321，响应ID=0x322。

| 字节 | 请求 | 响应 |
| --- | --- | --- |
| Byte0 | 0x51（Q） | 0x52（R） |
| Byte1 | 协议版本0x01 | 0x01 |
| Byte2 | 查询操作码 | 原样操作码 |
| Byte3 | 电脑请求标记，任意8位值 | 原样回显，仅用于配对，不是控制授权序号 |
| Byte4..7 | 必须全0 | uint32结果，小端，Byte4为最低字节 |

| 操作码 | 名称 | 返回值 |
| --- | --- | --- |
| 0x01 | GET_INFO | 0x0001001F：高16位查询结构版本1；低16位能力bit0 PING、bit1 INFO、bit2 STATUS、bit3 FAULT、bit4 BUILD |
| 0x02 | GET_STATUS | bit0..7状态；bit8..15标志；bit16..31禁止原因位，见下文 |
| 0x03 | GET_FAULT | board_fault_mask原始32位锁存故障，不清除故障 |
| 0x04 | GET_BUILD | 0x00000801：本次人工维护的软件接口修订号008.01，不是Git提交号或固件hash |

GET_STATUS低8位沿用axis_status：0 BOOT、1 ADC零偏标定、2 IDLE、3 ALIGNING、4软件输出运行、5 FAULT。中间标志字节bit0=命令核心配置有效（Bench合成配置也为1，不代表真实电机参数已确认），bit1=最近发布的软件命令许可镜像，bit2=软件输出使能标志，bit3=Bench，bit4=编译期禁功率输出；其余位0。高16位沿用AXIS_INHIBIT_*，不另建第二套控制判断。

状态查询表示诊断观察，不授予运行许可；运行状态不证明电机旋转，软件output标志不是电压测量。许可镜像可能尚未反映刚发生的故障/到期，以实时安全链为准。BUILD只区分本次协议修订，不能证明下载镜像与文件逐字节一致；产物hash在validation记录，源代码改变不会自动更新这个人工编号。

严格拒绝错误ID/DLC、扩展帧、远程帧、FD/BRS、未知版本/操作码、非零保留字节且不回复。硬件过滤的帧可能不进入软件、不会保证计数；进入FIFO后被DLC前护或协议校验拒绝的请求才增加rejected_count。此拒绝不会撤销已有命令或续期。未知查询无回包，电脑超时判断；还没有通用错误回执。重复查询允许，读取新的观察值，不构成控制重试。

### 6.2 有界服务与快照

- 保持原硬件过滤器和8B DLC前置保护；每轮最多1接收、1次发送入队；无重试、堆分配、队列积压软件副本或新任务/DMA。
- 原can_link_rx_count只数PING；新增can_query_rx_count只数合法查询；TX忙时仍计已接收查询并计错误，丢当前回复，电脑需重新查询。tx_enqueued_count不证明实际送达。
- CanLink_Init一次绑定只读状态provider，空provider为配置错误。INFO/BUILD只返回常量；STATUS/FAULT才调用一次provider。
- app_runtime静态ReadCanStatus在保存/恢复PRIMASK的短区内复制status_word、fault_mask、configured、permit、output_enabled五个诊断量，区外编码。副本在appTask栈，发送时HAL复制进FDCAN专用RAM。
- 该快照保证复制期间不被普通中断打断，**不保证五个镜像来自同一个控制拍或物理采样时刻**。故障可能先更新fault/output，permit下一次TIM1才刷新；NMI/Fatal不返回、不发送半份结果。每个查询独立取值，多个响应不能拼成一份同拍快照。
- 新短临界区纳入app_critical_max_cycles既有统计；本轮HOST不能证明实际屏蔽时长，需板测观察任务/FOC节拍。服务不调用Submit/Stop、FOC、故障清除或输出写接口。

### 6.3 已完成验证与用户操作

SRC独立复核通过；协议24507断言、服务32场景882断言通过。两目标增量Build均0错误0警告，仅重编app_runtime/can_link/can_link_protocol；原有Cygwin FAST_CWD环境警告不等于固件编译诊断。原始日志/源和产物指纹见[008记录](../validation/008-can-query-20261116.txt)。

Bench map：ROM 65764 B，静态RW+ZI 25984 B；相比007分别增加492 B、8 B。普通目标仍ROM60684 B、静态RW+ZI25712 B。新provider自身静态栈16 B，QueryValue自身32 B；函数指针调用和中断嵌套使这些数值不能证明任务最坏栈。无堆分配/新任务，appTask仍4096 B，完整最坏栈与临界区延迟未板测。

用户执行：

1. 保持功率母线断开、仅控制/传感器供电；退出旧调试，选`FOC_T_CommandBench`。本轮已编译成功；若本地又改了源码，用F7/Build，不用Rebuild All。手动Download，再进入调试Run，连续运行约5秒。
2. PCAN维持500 kbit/s、标准数据帧、DLC8；先取消旧PING的周期发送。新查询ID仍为321，下面三条逐条发送（Cycle Time=0，选中发送行按空格）。
3. 所有响应都用ID322，PCAN接收列表通常会覆盖同ID旧内容；看Byte2操作码和Byte3标记确认当前对应查询。不要同时给几条查询开周期发送。

| 顺序 | 发到321的数据（hex） | 应在322看到的数据 |
| --- | --- | --- |
| 版本BUILD | `51 01 04 01 00 00 00 00` | `52 01 04 01 01 08 00 00` |
| 状态STATUS | `51 01 02 02 00 00 00 00` | `52 01 02 02 SS FF LL HH`（后三类是占位说明，不可输入为hex） |
| 故障FAULT | `51 01 03 03 00 00 00 00` | 无故障时`52 01 03 03 00 00 00 00` |

STATUS稳定待机通常SS=02、FF=19：IDLE、Bench合成配置有效、许可镜像0、软件输出0、Bench/禁输出位1。自动演练期间许可镜像可能短暂为1而FF=1B，仍不允许功率输出。LL/HH为禁止原因位（常见01 05），不得一概要求全0：禁功率输出、PWM未准备、无命令在Bench待机是正常条件。若SS=05、FAULT非0或FF的输出位bit2为1，保留完整数据再排查，不转入启动测试。

4. 三条核对后，只把STATUS设成1000 ms周期，连续观察约30秒。Watch仅四项：`can_link_error_count`、`app_task_deadline_misses`、`foc_isr_overrun_count`、`ad2s_sample_gap_count`，预期均保持0。这些只支持本轮低速有限调度观察，不证明满负载或精确零丢包。
5. 本轮已提供STATUS与四项Watch截图，结果见第7节；BUILD/FAULT仅发送行及用户整体反馈保留。没有回包先核是否下载Bench及BUILD修订，不重复更改CubeMX或线路。

可选INFO请求`51 01 01 04 00 00 00 00`，回复`52 01 01 04 1F 00 01 00`。旧PING/PONG保持不变；`can_link_rx_count`不会因新查询增加，看查询需用`can_query_rx_count`（本轮不要求再加Watch）。

**本轮未关闭风险：** 新只读查询已获得有限正常观察，但短临界区最坏时长及负载时序尚待验证；状态镜像不是实时授权；BUILD不是镜像hash；TX满/Bus-Off无可靠回复或自动恢复。控制命令仍未开放，实际速度/电流范围与物理保护仍未验收。

## 7. 只读查询检查点（2026-11-16，用户反馈）

用户原话：“没问题我测试了几次发现都是正常没有error的计数增加”。

| 证据 | 可确认结果 | 边界 |
| --- | --- | --- |
| [STATUS截图](../validation/008-can-query-20261116-status.png) | 322回复`52 01 02 02 02 19 01 05`；操作码/标记对应STATUS请求 | 接收区仅当前STATUS，不能补写BUILD/FAULT回包数值 |
| STATUS解码 | IDLE；许可镜像0、软件输出0、Bench/编译禁功率位1；0x0501为PWM未准备/禁功率/无命令 | 核心配置有效仅指Bench夹具；不是物理输出测量或完整故障mask |
| [Watch截图](../validation/008-can-query-20261116-health.png) | ad2s_sample_gap_count、foc_isr_overrun_count、app_task_deadline_misses、can_link_error_count均0 | 用户报告多次正常，不据截图推断具体持续时间 |
| 其它查询 | BUILD/FAULT发送行可见；用户整体反馈正常 | 对应回包未留图；INFO无本轮可见结果 |
| 版本绑定 | 本地187项固件输入及Bench AXF/HEX与008记录一致 | 没有板端镜像hash回读；uvoptx用户调试变更保留 |

**结论：HW PASS（有限），第一步可形成提交检查点。** 当前图是Manual查询；1000 ms周期框未勾选，2378.6 ms是接收间隔显示，不是响应延迟或固定查询周期。所有业务共用322，累计105与发送各行次数没有统一统计窗口，不能计算丢包率。原始证据按追加方式保留在[008验证记录](../validation/008-can-query-20261116.txt)。

本轮收尾只更新文档并原样保存两张截图，没有修改固件、重复构建、提交或操作硬件。后续控制协议/STOP仍需实现；不得把只读查询通过扩展为运行许可、物理保护或电机闭环验收。

## 8. STOP 接入（008.02历史检查点，已提交98f736d）

**目标/已实现，正常低频禁输出检查点通过（有限）：** 以已提交的 `488d481` 为基线，复用现有 CAN 两模块和 app_runtime；不新增源/头文件，不修改 CubeMX。STOP 接入既有独立票据通道，关闭自动 Bench 发单，仍只在禁输出目标开放；START/UPDATE、故障清除和功率运行不开放。SRC/HOST/两目标增量BUILD通过，原始日志和187项生产输入/产物指纹已追加到既有008记录；用户2026-11-17两次反馈及截图见第8.6–8.7节；新编号接续已反馈正常。

### 8.1 字节合同和结果保留

标准数据帧、500 kbit/s、DLC=8、无 FD/BRS/RTR；请求仍 `0x321`，响应仍 `0x322`。旧 PING 和四个查询不变。INFO 改为 `0x0001003F`（新增 bit5=STOP）；BUILD 改为 `0x00000802`，仍是手工接口修订号，不是镜像指纹。

| 报文 | 8 字节内容 | 解释 |
| --- | --- | --- |
| STOP / 重读结果 | `43 01 10 00 id0 id1 id2 id3` | 非0 uint32 小端请求号，同次启动递增、不回绕；byte3 必须0 |
| STOP 回包 | `41 01 10 result id0 id1 id2 id3` | 回显完整请求号，不使用查询的8位token |

result：`01`=已接受，`02`=TIM1已处理，`03`=旧请求号，`04`=前一STOP待处理，`05`=交接入口拒绝。格式/ID/DLC/保留字非法、请求号0直接计拒绝而不回复；未实现的控制opcode同样拒绝，不猜测为STOP。

静态保留最近一次成功接受的 `request_id/stop_ticket` 两个uint32，只由appTask读写。同号重发不再调用Stop，只返回01/02；小于最近成功ID返回03；较大ID在旧STOP未处理时返回04且不覆盖，处理后可接入；入口拒绝返回05且不推进最近ID。成功后的第一次回复固定01，下一次同号重发才能得到02；请求号及内部票据只在本次启动有效，重启清空。没有跨复位session或通用命令回执队列，旧请求结果在更新ID成功后不再保留。未来允许START前必须重新审查会话/重放/取消及故障通信策略。

TX满或发送失败也先请求STOP，再保留结果；电脑可以重发同号补查。只在CanLink仍可服务时补查，不保证总线故障时收到结果。相同请求号完成后重发仅读历史结果，不是一次新的停止命令；新的停止意图必须使用更大的请求号。

### 8.2 谁执行、何时生效

appTask 的 `CanLink_Poll` 解码后调用注入的 StopRequester，由 app_runtime 转到 `AxisCommandPort_Stop`；通信代码不直接碰PWM、EN、FOC参数或故障。该入口以保存/恢复PRIMASK的短临界区挂独立STOP票据；TIM1的 `BeginCycle` 先经board_safety撤输出，再由core消费STOP、清除活动及待处理命令，最终 `FinalPermit/PublishDiagnostics` 发布已处理票据。

只读provider从20B扩展为24B，加入 `axis_command_stop_applied`：它是消费后才发布的单调完成标记，读取可能滞后，不能提前；只有与保留的ticket相等才返回02。其它五个诊断镜像仍不保证同一控制拍，也不是运行许可。结果02表示软件停止路径已处理，不表示电机物理停转或故障已清除。

自动 `AxisCommandBench_Init/Step` 从任务调度移除，原演练源与HOST回归保留；CAN是本目标唯一命令生产者。结合非0递增请求号、成功同号不重执行，保证一次启动内STOP调用次数不超过uint32票据容量；未来新增生产者必须重审，不能默许票据饱和复用。普通FOC_T仍无CAN服务或运行请求源。

### 8.3 时序边界与验收

仍每轮最多1接收、1回复入队，无等待/堆分配/DMA。STOP不绕过CAN仲裁、8槽FIFO和appTask的前置SPI；正常每轮消费一帧限制了单轮工作，不能保证满负载或SPI超时时的停车期限。Bus-Off仍锁停服务，未新增故障自动停机策略。本轮只验证禁输出软件链，受控减速、硬急停、运行中失联保护须在START开放前处理。

| 层级 | 状态 | 验收内容 |
| --- | --- | --- |
| SRC | PASS | 独立复核：唯一生产者、独立STOP、完成标记与复位边界 |
| HOST | PASS | 协议35533断言；服务47场景1830断言，含真实core消费/取消/未配置STOP/故障保持；基础回归610＋365断言及9配置组合通过 |
| BUILD | PASS | ARMCC5.06u7，两个目标均增量0错误0警告；各只重编3个.c |
| HW | PASS（有限） | 编号1的02回复有图；三项Watch及编号2接续/重复行为正常为用户反馈；非物理停车、满负载或最坏延迟验收 |

### 8.4 板测操作（本轮已反馈通过，保留复测说明）

像挂号和办结：首次回01只是拿到号码，重发同号回02才表示控制中断办完。无需改CubeMX。保持功率母线断开、仅控制/传感器供电；选 `FOC_T_CommandBench`，本轮已增量编译，退出旧调试后Download/复位/Run。若自行改了源码，F7 Build即可，不用Rebuild All。禁用所有旧周期发送，PCAN维持500 kbit/s、标准数据帧、DLC8、Cycle Time=0，以下全发321、看322。

| 步骤 | 发送（hex） | 预期回复 |
| --- | --- | --- |
| 1 核对新版本 | `51 01 04 01 00 00 00 00` | `52 01 04 01 02 08 00 00` |
| 2 请求STOP，编号1 | `43 01 10 00 01 00 00 00` | `41 01 10 01 01 00 00 00`（首次接受） |
| 3 等约1秒，再发原样编号1 | `43 01 10 00 01 00 00 00` | `41 01 10 02 01 00 00 00`（已处理） |
| 4 重复与新号 | 同号1再发，随后把Byte4改02，新号2发两次 | 同号一直回02且票据不加；新号先01再02、票据只再加1 |

从本轮固件复位开始、没有其它控制源时，Watch仅三项：
- `axis_command_stop_applied`：初始0；编号1处理后1，重复编号1仍1；编号2处理后2。它是内部已处理票据，并不等于任意填写的请求号（例如首个请求填100，票据仍从1开始）。
- `board_output_enabled`：始终0。
- `app_task_deadline_misses`：正常测试保持0。

若只做到前三步，可先反馈回包与三项Watch；第4步用于关闭“重复不重复执行/新请求可继续”的验收项。首次01不保证相机拍到票据仍0，TIM1可能已在回包到电脑前办完，下一次同号查询应回02。如果持续01、回03/04/05或看不到新版BUILD，保留回包与Watch排查，不直接进入START测试。旧axis_bench_stage不再运行；can_link_rx_count仅PING、can_query_rx_count仅查询，STOP不靠这两个数判断。

通过本轮只代表禁输出STOP交接与低频正常观察；不是带电机停车、故障清除、最坏延迟或满负载认证。板测后记录实际结果再由用户提交，本轮不代操作Git。

### 8.5 资源与本轮交付

链接map：Bench实际压缩ROM 63784 B，静态RW+ZI 25872 B；普通目标60684 B/25712 B。DTCM仍0、RW/ZI仍在AXI SRAM；RTOS堆16384 B、appTask栈4096 B不变。移除自动演练运行引用后，链接剔除其入口；不是新增动态分配。自身静态栈帧ReadCanStatus=16 B、RequestCanStop=8 B、HandleStop=40 B、CanLink_Poll=144 B；不能用这些数值代替函数指针、IRQ嵌套下的最坏任务栈验收。

008.02累计修改已有：固件5项、测试3项、文档/证据4项；本次仅新增1张原始回包截图，无新增源/头文件。编号2补测已反馈正常，由用户暂存这13项；`MDK-ARM/FOC_T.uvoptx`是已有的用户Watch设置改动，本轮未碰，可不随功能提交。构建产物与临时日志在已忽略的build/目标输出目录，不暂存。HW按具体用例分别记录，不能把正常低频用例通过写成全部异常边界或物理停车验收通过。

仍未关闭：真实STOP端到端/关中断最坏延迟、物理EN/MOE及实际停车；满流量/Bus-Off与运行授权的联动；跨复位session及START/UPDATE事务。本轮已完成的是禁输出软件停止链和结果保留。

### 8.6 编号1板测反馈与当时补测安排（2026-11-17；后续结果见8.7）

用户原话：“没问题watch也是跟你显示的一样这里测试没问题下一步”。

| 证据 | 可确认 | 边界 |
| --- | --- | --- |
| [STOP截图](../validation/008-can-stop-20261117.png) | 322/DLC8：`41 01 10 02 01 00 00 00`，编号1已处理；STOP发送行同号Manual Count8 | 当前仅02回包；首次01、BUILD具体返回值没有留图 |
| Watch反馈 | 用户报告与三项预期一致：stop_applied到1且同号不增加；output_enabled=0、deadline_misses=0 | 用户自述，无独立Watch截图，不扩大到其它计数或精确持续时间 |
| 本地版本 | 187项生产输入及Bench HEX与008.02记录一致 | AXF容器hash不同，原因未查明；原/现hash已追加证据，不宣称板端镜像已回读 |
| 当时待确认（已关闭） | 编号2接续 | 本图只含编号1；后续用户正常反馈及收尾结论见8.7 |

结论：**编号1处理/同号重读 HW PASS（有限）**。Cycle12712.0不是STOP响应延迟，Count109与发送8没有统一统计窗口，不计算丢包率。本次仅更新既有文档并保存1张原始截图，固件未改，不重复编译/测试。

当时补测安排（已获正常反馈，见8.7）：只补既定第4项，不重跑编号1：**继续当前Run，不复位、不重新下载**；仍发321、看322、手动/DLC8，发送 `43 01 10 00 02 00 00 00`，等约1秒再发原样一次。预期先 `41 01 10 01 02 00 00 00`，后 `41 01 10 02 02 00 00 00`。继续原三项Watch：若当前票据1，应变2且重发不再涨；输出和任务超时仍0。若已复位，票据会重置，按“新请求只加一次、同号不再加”验收，不机械要求票据等于请求号。

补测通过即可形成STOP提交检查点，建议提交消息 `feat: 接入 CAN STOP 与结果重读，完成禁输出台架检查`。随后继续008中的整组速度/Iq命令与会话/超时设计，保持禁输出；本轮不开放电机启动。

### 8.7 编号2补测与STOP检查点收尾（2026-11-17）

用户在收到编号2两次发送及原三项Watch要求后回复：“可以没问题显示正常”。据此记录编号2新请求接续、同号重读不重复执行、输出和任务超时观察正常，均属于用户反馈；本次无新截图/原始总线记录，不补造精确票据值、是否复位或运行时长。

**结论：008.02正常低频禁输出STOP检查点 HW PASS（有限），可以提交。** 编号1已处理回复有截图，编号2及Watch结果有用户确认；接受与处理分离、结果重读和连续接新请求的正常流程已覆盖。真实故障/拥塞、跨复位session、物理停车/EN/MOE、最坏延迟/栈及完整运行保护仍按原边界保留，不能据此开放功率。

本次只更新4个已有文档/证据文件，新增0项，187项生产输入与既有构建一致，不重复编译。累计待提交仍为13项：固件5、测试3、既有文档/证据4、原始STOP截图1；另1项uvoptx为用户本地Watch设置，可不随功能提交。建议消息：`feat: 接入 CAN STOP 与结果重读，完成禁输出台架检查`。

下一开发阶段是整组速度目标/Iq上限的纯C线路事务：先明确字段/单位、请求与会话身份、齐包/超时及重放规则，再接入既有AxisCommandPort，继续保持Bench禁功率。沿用本spec与现有模块，不新建一套源/头文件；本节只说明下一步，未实现START/UPDATE。

## 9. 整组速度/Iq参数预检（008.03，2026-11-17）

**本轮目标/已实现，正常低频禁输出板测通过（有限，9.7）：** 基线`98f736d`（STOP已提交）。先固定单位、单帧配对和共用限值校验，复用现有模块/测试；不新增源/头文件。原拟直接接控制中断，但审查发现跨复位身份、FIFO年龄与运行回执未闭合，因此本轮只交付完整接收/参数校验，**不调用Submit，不开放CAN START/UPDATE，不宣称TIM1已接单**。待机和STOP路径保留。

### 9.1 线路合同

经典标准数据帧500 kbit/s，DLC8，请求321、响应322；原PING、四查询和STOP兼容。INFO修订为`0x0001007F`（bit6=整组预检），BUILD=`0x00000803`，仍不是镜像hash。

| 字节 | 请求 | 响应 |
| --- | --- | --- |
| 0..3 | `43 01 20 token` | `56 01 result token`，56是预检回包，不是运行ACK |
| 4..5 | signed int16速度目标，rpm，小端 | 原样回显 |
| 6..7 | unsigned uint16 Iq上限，mA，小端 | 原样回显 |

token只用于回复配对，0..255均可，不是会话/授权/去重序号。result固定为1=参数范围通过、2=参数越界、3=核心配置未确认、4=内部拒绝；不透传core枚举的数值布局。单帧即含整组，不分多帧组装；负数显式转int32，避免实现相关的窄类型转换。格式/元数据非法直接拒绝不回；格式合法但越界仍回显原参数和result2，不悄悄限幅。

实际检查复用axis_command.c私有command_limits：Bench±1000 rpm、Iq 0..4000 mA仅是合成夹具，不能作为真实电机参数；普通配置未确认时返回3。RANGE通过不校验实时ready、反馈年龄、fault、epoch、运行期限，也不授予启动许可。Iq为0沿用原核心合法规则。

### 9.2 所有权与调用边界

FDCAN FIFO→appTask私有报文副本（8B载荷）→codec完整解码一对整数→app_runtime回调转换rpm/安培→只读`AxisCommandPort_CheckTargets`→共享纯函数`AxisCommand_ValidateTargets`。原完整帧Validate也调用同一参数校验函数；只维护一份限值/规则，无合成epoch/租期。预检不读写控制邮箱、active、seq、epoch或许可，不缓存可供稍后START提交的“已批准参数”。返回后HAL复制完整回复到TX RAM；仍每轮最多1收/1次回包入队，无等待/堆/DMA/新增任务。

Port入口保持appTask准入；核心纯函数不读运行状态，const限值在启动前固定。CAN Init一次绑定第三个只读checker，任一provider缺失即失败；checker异常返回映射result4并锁存CONFIG通信错误。重复请求重新检查/回显；丢回包可原样重试，既不执行控制也不续期，TX满不积压软件回复。

本轮没有控制命令的“生效时刻”：仅出队后对本地完整帧做参数预检，不能把出队时间当接收时刻。滞留或复位前请求也只会重复预检，没有运行副作用；因此本轮不使用DWT/UID假称唯一启动身份，也不引入临时弱session。

### 9.3 START/UPDATE前的必需合同

以下仍是方案/待实施，不能以本轮通过代替：
- 会话身份与跨复位旧帧：当前未启用RNG/RTC，也无持久启动代号；必须定可验证的会话建立/失效规则，不能仅靠归零seq或DWT。
- 排队年龄：本地HAL已有SOF捕获`RxTimestamp`及Config/EnableTimestampCounter接口。500k、PRESC16的16bit计数约2.097152s回绕；单纯模差不能识别整圈滞留，需服务停顿上界/过期丢弃与授权失效规则。本轮未启用时间戳或改CubeMX。
- 结果保留：GetSnapshot不是事件日志；后续按applied_seq记录应用事实、cancelled_seq识别被取消pending，保留请求到内部seq映射；普通失败可能覆盖result_seq，不能只盯最后一个result。
- 完整速度/Iq帧必须带确定的授权身份和新鲜度，当前预检token不能升级为运行授权。只允许新鲜合法更新续期；查询/重复不续期；STOP、到期、Bus-Off和恢复边界需联合验收。

### 9.4 验收计划

| 层级 | 状态 | 内容 |
| --- | --- | --- |
| SRC | PASS | 独立审查：单帧完整、共享规则、无控制副作用、保留原STOP；上下文拒绝映射已修正 |
| HOST | PASS | 协议70347断言，服务58场景2475断言，端口/演练/安全431断言＋9配置；原core203＋FOC407回归通过 |
| BUILD | PASS | ARMCC5.06u7，普通/Bench两个目标增量0错误0警告；最终指纹见证据追加 |
| HW | PASS（有限） | 用户确认BUILD008.03、100rpm/500mA结果01、4001mA结果02；applied_seq/output/deadline_misses均0，见9.7；无满负载或物理输出验收 |

### 9.5 本轮最少板测步骤（已获用户结果，见9.7）

保持功率母线断开、仅控制/传感器供电，选`FOC_T_CommandBench`，本轮已完成增量编译。退出旧调试后Download、复位、Run；如自行改了源码用F7 Build，不用Rebuild All。先取消旧周期发送；PCAN500 kbit/s、标准数据帧、DLC8、手动，以下发321、看322。

| 用例 | 发送hex | 预期回包hex |
| --- | --- | --- |
| 确认BUILD008.03 | `51 01 04 01 00 00 00 00` | `52 01 04 01 03 08 00 00` |
| 100rpm＋500mA | `43 01 20 01 64 00 F4 01` | `56 01 01 01 64 00 F4 01` |
| 100rpm＋4001mA越界 | `43 01 20 02 64 00 A1 0F` | `56 01 02 02 64 00 A1 0F` |

`64 00`是100rpm，`F4 01`是500mA＝0.5A，`A1 0F`是4001mA＝4.001A。回复第3字节01表示范围通过、02表示越界；第4字节回显token，最后4字节原样回显，不能把01理解为命令已应用。越界是预期结果，不是通信故障，不应因此增加can_link_error_count。

Watch只保留三项：`axis_command_applied_seq`、`board_output_enabled`、`app_task_deadline_misses`，本轮从复位后都应保持0。第一项0表示没有把预检当运行命令应用；即使参数检查通过也不应变成1。若有非0或回包不符，保留结果排查，不转入启动测试。

可选独立补充（不必第一轮全部发送）：负速度-100rpm/500mA请求`43 01 20 03 9C FF F4 01`，应回`56 01 01 03 9C FF F4 01`；旧STOP编号1两次仍按第8节先01后02，不影响axis_command_applied_seq。现有计数can_link_rx_count只计PING、can_query_rx_count只计四个旧查询，预检不靠这两项判断。

本轮边界：正常参数通过不检查实时ready、故障或角度年龄，不代表电机运行范围已确认；Bench限值仍为合成测试范围。重复预检/旧帧不产生运行副作用，但会话和队列年龄尚未解决。无新DMA/IRQ/任务；完整最坏耗时、栈、物理功率和运行失联保护仍待后续。

### 9.6 资源、实现差异与交付范围

最终链接map：Bench压缩ROM64332 B、静态RW+ZI25880 B，较008.02增加548 B/8 B；普通目标保持60684 B/25712 B。DTCM使用0，RW/ZI仍在AXI SRAM；既有16KiB RTOS堆、appTask4KiB栈不变，无新堆分配、任务或DMA。自身函数栈帧见原始证据；函数指针和IRQ嵌套使它们不能代表最坏任务栈。

原完整Validate保持配置→空帧→序号→模式→opcode/epoch/参数→时间的拒绝顺序；共享ValidateTargets会再次检查配置，因此行为等价不代表cycle开销相同。预检不进PRIMASK，不访问command_core；错误任务/ISR/内核状态返回新增INVALID_CONTEXT（在旧枚举末尾），线路映射内部拒绝4，避免误报越界2。

本轮修改已有文件：固件9、测试5、文档/证据4；新增0。所有源/头文件延续现有模块，只增加共享范围函数及只读包装，避免通信层复制阈值。用户的uvoptx修改保留，不随本轮功能暂存；板测后再由用户提交其它18项。旧证据追加保留，构建/临时日志不入Git。当前正常低频禁输出HW PASS（有限，见9.7），不沿用STOP旧板测补充新镜像证据。


### 9.7 通信恢复及预检检查点收尾（2026-11-17）

用户原话：“恢复了确实重启了PCAN就没问题了，然后测了你发的三组数据都能发送回复”。随后明确确认：版本为008.03，100 rpm/500 mA返回结果01，100 rpm/4001 mA返回结果02；保持当前Run、不复位查看三项Watch，回答“三项都是0”。

| 验收项 | 本轮结果与证据 | 边界 |
| --- | --- | --- |
| 电脑端通信恢复 | 用户报告重启PCAN后恢复；排查未修改固件、未重新构建 | 具体原因未确认；未明确是否USB断电，不补记该操作，不认定为PCAN/驱动/适配器某一层缺陷 |
| BUILD / 合法 / 越界 | 用户确认008.03、01、02与本轮三项要求一致 | 用户反馈，无本轮成功回包截图或原始Trace文件，不补造逐帧时序/次数 |
| 预检无运行副作用 | 用户确认axis_command_applied_seq=0、board_output_enabled=0 | 软件观测；不是实际EN/MOE电平、物理保护或电机运行验收 |
| 调度观察 | 用户确认app_task_deadline_misses=0 | 有限测试窗口；未提供精确持续时间，不能证明满负载或最坏实时性 |
| 本地版本绑定 | 187项固件输入与008.03构建清单一致，Bench HEX保持一致；具体指纹追加008原始记录 | BUILD为手工接口修订号；未回读板端Flash hash，AXF容器差异仍须区分装载内容 |

排查时旧PING已使板端接收/回复入队计数同步增加，TXBTO随手动发送从3变7，TXBRP为0；PCAN过滤已确认Standard、000～7FF，但Trace仅记录Tx321。它们支持沿电脑端接收链继续排查，不能单凭入队或TXBTO证明PCAN应用已收到。后来重启PCAN后恢复，故仅记录操作与结果的相关性；早期error_flags=2和后续0不构成根因证明。

**结论：008.03正常低频禁输出参数预检检查点HW PASS（有限），可提交。** 三项回包及原清单三项Watch均已由用户确认；没有新增测试门槛。合法参数返回01只表示范围通过，不代表控制中断应用或运行许可；Bench合成范围不作为实际电机规格。

本次只更新4个已有文档/证据文件，新增0，未修改固件、重新编译、代提交或操作硬件。累计功能变更18项（固件9、测试5、文档/证据4），另有用户本地MDK-ARM/FOC_T.uvoptx设置改动，建议单独处理。建议提交消息：`feat: 接入 CAN 速度与 Iq 参数预检并完成禁输出台架检查`。

下一阶段按9.3完成运行命令会话、排队年龄与过期、结果保留/重读合同，再在禁输出目标接入START/UPDATE；不把本轮预检token或PCAN重新连接直接作为运行授权。
