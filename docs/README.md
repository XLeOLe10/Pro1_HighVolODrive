# FOC_T 项目入口

**现在：** 005已提交到`1d3f101`。006第一段已实现：FOC阶段由私有量派生，修正待机却显示对齐的问题；两目标增量与610项HOST断言通过，四项禁输出板测截图符合预期，006第一段可提交。

**当前功能：** [006 Axis运行阶段契约](specs/006-axis-run-phase.md)。**接下来：** 用户提交006第一段检查点；无需重复四项回归或栈开关A/B。首个真实通信接口尚未选定。

**日常先看本页，再看当前spec。** 当前分工/数据/输出权限统一查 [ARCHITECTURE.md](ARCHITECTURE.md)。

## 当前版本与最新变化

- 分支Xiao_ODrive，005已提交为`1d3f101`；006以此为干净基线。005证据保留在[原验证记录](validation/005-command-bench-20261109.txt)，不自动用于006硬件通过。
- 同一工程两个Keil目标、独立产物：普通FOC_T保持未确认配置；FOC_T_CommandBench使用合成测试范围并强制禁功率输出。
- 006两目标最新增量均0错误0警告，只重编open_foc/app_runtime；新增阶段接口不授予许可，TIM1/board_safety权威保留。005的遥测格式化与Idle栈统计继续保留。
- 006实际Core203＋FOC407断言通过；[新证据](validation/006-axis-run-phase-20261110.txt)已追加四项[截图](validation/006-axis-run-phase-20261110-watch.png)：stage=100、aligning/fault/output均0，只支持禁输出待机/演练的有限观察。
- 旧Watch run/step依旧不能启动。软件permit可在测试期间为1，输出仍必须为0。
- 原005待机有限观察及HEX/AXF差异保留在[旧记录](validation/005-command-handoff.txt)；002/003旧记录同样保留。

## 文档与证据怎么找

| 想看什么 | 唯一入口 |
| --- | --- |
| 当前实现：谁采样、谁计算、谁能开输出 | [当前架构](ARCHITECTURE.md) |
| 当前改动、验收、下一组关键参数 | [006 spec](specs/006-axis-run-phase.md)；前一检查点[005](specs/005-command-handoff.md) |
| 当前构建/主机测试与指纹 | [006记录](validation/006-axis-run-phase-20261110.txt)；此前[005记录](validation/005-command-bench-20261109.txt) |
| 上一阶段原始截图及有限观察 | [002台架记录](validation/002-axis-status-bench.md) |
| 上一阶段构建与AXF差异 | [003构建记录](validation/003-architecture-build.txt) |
| 命令方案来源、后续时序/DMA | [003方案](specs/003-c-port-architecture.md)；已实现部分以005和架构页为准 |
| 文档职责、RTOS/Git历史 | [004](specs/004-docs-organization.md)、[001](specs/001-rtos-preflight.md)、[000](specs/000-git-workflow-demo.md) |
| 长期约束与ODrive参考 | [参考稿](reference/odrive-porting-guidelines-draft.md) |

## 未关闭事项

| 事项 | 下一步 / 证据边界 | 回链 |
| --- | --- | --- |
| 006证据边界 | 四项禁输出观察通过；Getter的实际TIM1/临界区最坏开销未测，ALIGN/CLOSED转换仅HOST验证 | [006](specs/006-axis-run-phase.md) |
| 005证据边界 | 本轮禁输出演练及空闲统计观察已收尾；发布临界区、物理输出、完整IRQ最坏耗时及长期稳定性仍未验收 | [005](specs/005-command-handoff.md) |
| 完整时序与资源 | 停止路径不代表闭环最坏耗时；MSP、嵌套IRQ、完整关中断时长、最坏任务栈仍待验证 | [001](specs/001-rtos-preflight.md)、[架构](ARCHITECTURE.md) |
| 实际输出与保护 | EN/MOE/PWM、死区、真实关断延迟、故障注入及运行态未验收；Break关闭，AD2S器件故障仍仅诊断 | [001](specs/001-rtos-preflight.md)、[002](specs/002-axis-status-observer.md) |
| 电流和角度对应 | ADC源帧/时刻、量程/极性、角度零位与theta_i/theta_pwm待核实；标称dt与实际周期差异保留 | [架构](ARCHITECTURE.md)、[003](specs/003-c-port-architecture.md) |
| 必要硬件及运行配置 | 最高转速、实际电机/旋变极对数、角误差、Iq/速度范围、期限和保护阈值未定；不以HOST夹具代填 | [005](specs/005-command-handoff.md)、[参考约束](reference/odrive-porting-guidelines-draft.md) |
| 通信、DMA和存储 | 只有测试目标的单轮生产者和一致快照；无真实协议/可靠ACK队列/跨复位会话；DMA池/MPU/cache合同、CAN/ETH/RS485/Flash分功能推进 | [003](specs/003-c-port-architecture.md)、[005](specs/005-command-handoff.md) |
| 原有证据边界 | 旧AXF差异原因未知；截图无板端固件指纹回读；cache实际状态与完整采样间隔待测 | [003证据](validation/003-architecture-build.txt)、[002记录](validation/002-axis-status-bench.md) |

实现与验证分开：源码/编译/主机/板测分别判定。原始证据在validation，功能验收表在spec。
