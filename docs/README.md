# FOC_T 项目入口

当前工作区为 **008.17**。禁输出 Bench 在采样任务中成对保留速度遥测数据，借用现有空闲任务完成 CSV 格式化和串口发送。此前一次板上捕获的采样后工作段为 849.070 µs，008.16 的一次同类记录为 1150.985 µs。

008.17 禁输出板测已收尾，完整 START/UPDATE 演练未通过。最后一次配对记录显示，相邻旋变 SAMPLE 间隔 2088.375 µs，超过 2 ms 门槛。已登记反馈采样时序阻塞。本阶段转入 ODrive Axis 离线验证，命令核心及 FOC 集成本次通过 1019 条断言。[板测收尾条件](specs/008-can-command-protocol.md#1314-禁输出命令演练的验收边界)

**新板存储：** 已确认STM32H750＋W25Q32JV（4 MiB）。[003第10节](specs/003-c-port-architecture.md#10-新板存储与程序执行规划2026-11-17拟议未实施)给出分区预算、掉电保存和后续启动搬运草案；存储部分仍为规则/方案，未启用QSPI或改链接布局；当前推进 Axis 离线验证。

**日常先看本页，再看当前spec。** 当前分工/数据/输出权限统一查 [ARCHITECTURE.md](ARCHITECTURE.md)。

## 当前版本与最新变化

- 分支Xiao_ODrive，当前基线`0489733`（008.04–06已提交）；008.17为当前工作区。历史008.03为`30aa8d1`、008.02为`98f736d`、008.01为`488d481`。
- 同一工程两个Keil目标、独立产物：普通FOC_T保持未确认配置；FOC_T_CommandBench使用合成测试范围并强制禁功率输出。
- 006两目标最新增量均0错误0警告，只重编open_foc/app_runtime；新增阶段接口不授予许可，TIM1/board_safety权威保留。005的遥测格式化与Idle栈统计继续保留。
- 006实际Core203＋FOC407断言通过；[新证据](validation/006-axis-run-phase-20261110.txt)已追加四项[截图](validation/006-axis-run-phase-20261110-watch.png)：stage=100、aligning/fault/output均0，只支持禁输出待机/演练的有限观察。
- 008.03协议70347断言、服务58场景2475断言；core203＋FOC407、端口/演练/安全431断言与9配置通过。两个目标增量0错误0警告；008.03三组回包和三项Watch获用户确认，HW PASS（有限）。
- 008.04身份HOST3576、协议84080、服务70场景4868断言通过；两目标增量0错误0警告。新增7个生成RNG文件及1个HOST脚本，没有新增业务模块/任务；两轮HW有限通过，第二轮只有首组截图与按清单文字确认，详见10.8。
- 008.05在原core/port扩展12 B处理记录，原Snapshot72 B不变；新增文件0。HOST core612＋FOC407、端口/演练/安全717＋9配置、协议84080/服务4868断言通过；两目标增量0错误0警告；805版本回包有限通过，内部回执/临界区仍未板验。
- 008.06扩展原CAN模块，0A/0B可查时间状态/年龄；HOST协议89573、服务147场景12116断言通过，两目标增量0错误0警告。Bench ROM66420/RAM25968 B；新增文件/任务0。HW PASS（有限）：CCFG/年龄有截图，20秒正常轮及暂停/复位状态由用户文字确认；未独立测量计时精度或最坏耗时，详见12.6。
- 008.07：9帧组包＋结果重读、原始SOF期限贯穿TIM1、task失效序号防绕回复活；只扩展7个原业务文件/5个原HOST文件，新增1个电脑测试脚本。HOST协议135945、服务215场景26631、port84/636均通过；两目标0错误0警告。Bench ROM69968/RAM26136 B，普通目标不变。用户截图支持接口、首单到期及历史结果重读的有限观察；最新启动缺少配套发送截图与3项Watch，不扩写为完整联调通过。
- 原005待机有限观察及HEX/AXF差异保留在[旧记录](validation/005-command-handoff.txt)；002/003旧记录同样保留。

## 文档与证据怎么找

| 想看什么 | 唯一入口 |
| --- | --- |
| 代码职责分类、常用文件入口 | [源码分类导航](ARCHITECTURE.md#源码分类导航) |
| 当前实现：谁采样、谁计算、谁能开输出 | [当前架构](ARCHITECTURE.md) |
| 当前实现/板测结果及下一步 | [008第13节当前事务/板测，第10–12节历史检查点](specs/008-can-command-protocol.md)；第6–8节是008.01/02历史；已完成的[007检查点](specs/007-can-link-bringup.md) |
| 当前构建/主机测试与指纹 | [008记录](validation/008-can-query-20261116.txt)；旧板测在[007记录](validation/007-can-link-20261112.txt) |
| 上一阶段原始截图及有限观察 | [002台架记录](validation/002-axis-status-bench.md) |
| 上一阶段构建与AXF差异 | [003构建记录](validation/003-architecture-build.txt) |
| 命令方案来源、后续时序/DMA/新板存储 | [003方案](specs/003-c-port-architecture.md)；已实现部分以005和架构页为准 |
| 文档职责、RTOS/Git历史 | [004](specs/004-docs-organization.md)、[001](specs/001-rtos-preflight.md)、[000](specs/000-git-workflow-demo.md) |
| 长期约束与ODrive参考 | [参考稿](reference/odrive-porting-guidelines-draft.md) |

## 未关闭事项

| 事项 | 下一步 / 证据边界 | 回链 |
| --- | --- | --- |
| 007验证边界 | 1 Hz PING/PONG及四项异常计数观察已通过；适配器版本/隔离/终端、实际时钟/电气裕量、精确统计、异常帧/满负载及最坏栈仍未独立验收 | [007](specs/007-can-link-bringup.md) |
| 006证据边界 | 四项禁输出观察通过；Getter的实际TIM1/临界区最坏开销未测，ALIGN/CLOSED转换仅HOST验证 | [006](specs/006-axis-run-phase.md) |
| 005证据边界 | 本轮禁输出演练及空闲统计观察已收尾；发布临界区、物理输出、完整IRQ最坏耗时及长期稳定性仍未验收 | [005](specs/005-command-handoff.md) |
| 完整时序与资源 | 停止路径不代表闭环最坏耗时；MSP、嵌套IRQ、完整关中断时长、最坏任务栈仍待验证 | [001](specs/001-rtos-preflight.md)、[架构](ARCHITECTURE.md) |
| 实际输出与保护 | EN/MOE/PWM、死区、真实关断延迟、故障注入及运行态未验收；Break关闭，AD2S器件故障仍仅诊断 | [001](specs/001-rtos-preflight.md)、[002](specs/002-axis-status-observer.md) |
| 电流和角度对应 | ADC源帧/时刻、量程/极性、角度零位与theta_i/theta_pwm待核实；标称dt与实际周期差异保留 | [架构](ARCHITECTURE.md)、[003](specs/003-c-port-architecture.md) |
| 必要硬件及运行配置 | 最高转速、实际电机/旋变极对数、角误差、Iq/速度范围、期限和保护阈值未定；不以HOST夹具代填 | [005](specs/005-command-handoff.md)、[参考约束](reference/odrive-porting-guidelines-draft.md) |
| 通信命令 | 008.04–06有限检查点已提交0489733。008.07 START/UPDATE完整事务、身份/epoch/时限与回执缓存已实现；SRC/HOST/BUILD通过；HW PASS（有限：接口/首单到期/历史结果）。START→UPDATE在008.07因NOT_READY失败；008.08复测仍FAIL；008.09完整演练ADC_FRAME失败；008.10单文件O1板测仍ADC_FRAME失败；008.11已捕获ADC与NOT_READY不同启动现场，完整演练FAIL；008.12有界补跑软件验证通过，但演练仍反馈超龄FAIL；008.13 runtime单文件O1板测仍ADC_FRAME失败；008.14板测因旋变反馈超龄5.28 µs撤销许可；008.15 配对捕获有效，完整演练因反馈超龄失败；008.16 已在板上取得三段耗时，合计 1999.200 µs，完整演练仍失败；008.17 最终配对显示下一次采样来晚，完整演练因代次不匹配失败，已登记阻塞并结束本轮板测；STOP打断及异常事务仍NOT_RUN；普通目标尚未启用，真实停机、满负载和最坏时序仍待验；自定义协议不兼容CANSimple，完整Axis生命周期未完成 | [008第13节](specs/008-can-command-protocol.md) |
| DMA和存储 | 无业务DMA/QSPI存储实现；新板Flash型号已确认，第9/10节为DMA与存储草案，分区/启动/掉电验证待实施；不阻塞旧板控制主线 | [003](specs/003-c-port-architecture.md) |
| 原有证据边界 | 旧AXF差异原因未知；截图无板端固件指纹回读；cache实际状态与完整采样间隔待测 | [003证据](validation/003-architecture-build.txt)、[002记录](validation/002-axis-status-bench.md) |

实现与验证分开：源码/编译/主机/板测分别判定。原始证据在validation，功能验收表在spec。用户的MDK-ARM/FOC_T.uvoptx调试设置变更保留，暂存时单独判断。
