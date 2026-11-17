# FOC_T 项目入口

**现在：** STOP检查点已提交`98f736d`。008.03已加入速度/Iq整组参数预检和回显，复用命令层同一限值；源码/主机/两目标增量编译通过；用户已确认BUILD、合法/越界回包及三项Watch，正常低频禁输出板测通过（有限）。范围通过不代表启动许可，START/UPDATE仍未接入。

**当前功能：** [008第9节整组预检](specs/008-can-command-protocol.md)。**接下来：** 用户提交本轮18项功能变更，uvoptx本地设置单独处理；本轮结果见9.7。会话/新鲜度合同完成后再接运行命令。

**日常先看本页，再看当前spec。** 当前分工/数据/输出权限统一查 [ARCHITECTURE.md](ARCHITECTURE.md)。

## 当前版本与最新变化

- 分支Xiao_ODrive，当前基线`98f736d`（008.02）；008.01为`488d481`，007为`350ef94`。本轮预检为工作区增量，历史板测不自动支持新固件。
- 同一工程两个Keil目标、独立产物：普通FOC_T保持未确认配置；FOC_T_CommandBench使用合成测试范围并强制禁功率输出。
- 006两目标最新增量均0错误0警告，只重编open_foc/app_runtime；新增阶段接口不授予许可，TIM1/board_safety权威保留。005的遥测格式化与Idle栈统计继续保留。
- 006实际Core203＋FOC407断言通过；[新证据](validation/006-axis-run-phase-20261110.txt)已追加四项[截图](validation/006-axis-run-phase-20261110-watch.png)：stage=100、aligning/fault/output均0，只支持禁输出待机/演练的有限观察。
- 008.03协议70347断言、服务58场景2475断言；core203＋FOC407、端口/演练/安全431断言与9配置通过。两个目标增量0错误0警告；008.03三组回包和三项Watch获用户确认，HW PASS（有限）。
- 自动axis_bench_stage不运行；预检不得增加axis_command_applied_seq或打开输出。STOP仍看axis_command_stop_applied；具体最少Watch见9.5。
- 原005待机有限观察及HEX/AXF差异保留在[旧记录](validation/005-command-handoff.txt)；002/003旧记录同样保留。

## 文档与证据怎么找

| 想看什么 | 唯一入口 |
| --- | --- |
| 代码职责分类、常用文件入口 | [源码分类导航](ARCHITECTURE.md#源码分类导航) |
| 当前实现：谁采样、谁计算、谁能开输出 | [当前架构](ARCHITECTURE.md) |
| 当前实现/板测结果及命令对照 | [008第9节](specs/008-can-command-protocol.md)；第6–8节是008.01/02历史；已完成的[007检查点](specs/007-can-link-bringup.md) |
| 当前构建/主机测试与指纹 | [008记录](validation/008-can-query-20261116.txt)；旧板测在[007记录](validation/007-can-link-20261112.txt) |
| 上一阶段原始截图及有限观察 | [002台架记录](validation/002-axis-status-bench.md) |
| 上一阶段构建与AXF差异 | [003构建记录](validation/003-architecture-build.txt) |
| 命令方案来源、后续时序/DMA | [003方案](specs/003-c-port-architecture.md)；已实现部分以005和架构页为准 |
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
| 通信命令 | 008.01查询有限观察通过；008.02 STOP有限检查点已提交；008.03整组预检正常低频禁输出板测通过（用户确认）；重启PCAN后恢复通信，具体原因未确认。预检不发布运行命令；异常负载/真实停机仍未验收。自定义协议不兼容CANSimple；START/UPDATE整组事务、跨复位session、运行失联策略及STOP最坏排队延迟未闭合 | [008](specs/008-can-command-protocol.md) |
| DMA和存储 | 无业务DMA；DMA池/MPU/cache、ETH/RS485/Flash后续独立推进 | [003](specs/003-c-port-architecture.md) |
| 原有证据边界 | 旧AXF差异原因未知；截图无板端固件指纹回读；cache实际状态与完整采样间隔待测 | [003证据](validation/003-architecture-build.txt)、[002记录](validation/002-axis-status-bench.md) |

实现与验证分开：源码/编译/主机/板测分别判定。原始证据在validation，功能验收表在spec。
