# FOC_T 项目入口

**现在：** RTOS已接入；002运行状态观测已实现，母线隔离条件下的三组待机基础观察已收尾，可继续下一模块的软件开发。完整启停管理、整组命令和DMA尚未实现。

**当前功能：** [002运行状态观测](specs/002-axis-status-observer.md)。**接下来：** 按003方案进入统一命令交接模块的功能spec与纯软件测试；不再追加当前待机Watch清单，不启动输出。

**日常先看本页，再看当前spec。** 当前分工/数据/输出权限统一查 [ARCHITECTURE.md](ARCHITECTURE.md)；本次文档整理见 [004](specs/004-docs-organization.md)。

## 当前版本与最新变化

- 分支Xiao_ODrive，提交1668486（RTOS基线），另有002实现及文档的暂存/未暂存改动；不是已经提交的新固件版本。
- 002三组有限观察已记录：待机/输出关闭/计数推进、错误计数未报告新增、停止路径耗时及任务栈/堆余量。支持继续软件开发，不是完整闭环或保护验收。
- 本次按用户文档规则建立单一入口和当前架构；保留旧路径、未关闭事项与原始证据，不改固件，不重复编译，不操作暂存区。
- 003原AXF指纹不匹配、原因未知的发现保留；之后全量重建证据另记，不能追溯抹去差异。

## 文档与证据怎么找

| 想看什么 | 唯一入口 |
| --- | --- |
| 当前实现：谁采样、谁计算、谁能开输出 | [当前架构](ARCHITECTURE.md) |
| 当前测试、下一小组参数、该功能验收 | [002 spec](specs/002-axis-status-observer.md) |
| 当前原始截图、读数及证据边界 | [002台架记录](validation/002-axis-status-bench.md) |
| 最近成功构建/内存/指纹及AXF差异 | [003构建记录](validation/003-architecture-build.txt) |
| 后续纯C命令、时序、DMA方案 | [003方案](specs/003-c-port-architecture.md)（拟议，未实现） |
| 本次文档整理前后对照及检查 | [004 spec](specs/004-docs-organization.md) |
| RTOS接入前/后历史及原有板测 | [001 spec](specs/001-rtos-preflight.md) |
| 长期约束ID与ODrive参考 | [参考稿](reference/odrive-porting-guidelines-draft.md) |
| Git教学历史 | [000 demo](specs/000-git-workflow-demo.md)，内嵌旧规则不生效 |

## 未关闭事项

| 事项 | 下一步 / 证据边界 | 回链 |
| --- | --- | --- |
| 002后续验证 | 基础观察收尾；临界区、任务期限、采样间隔与实际cache状态按后续模块依赖安排，不阻塞纯软件命令开发 | [002](specs/002-axis-status-observer.md)、[原始记录](validation/002-axis-status-bench.md) |
| 完整时序与资源 | 停止路径不代表闭环最坏耗时；MSP、嵌套IRQ、关中断完整时长、最坏任务栈仍待验证 | [001](specs/001-rtos-preflight.md)、[架构](ARCHITECTURE.md) |
| 实际输出与保护 | EN/MOE/PWM、有效死区、真实故障关断延迟、故障注入及运行态未验收；Break关闭，AD2S器件故障仍仅诊断 | [001](specs/001-rtos-preflight.md)、[002](specs/002-axis-status-observer.md) |
| 电流和角度对应 | ADC源帧/采样时刻、量程/极性、角度零位与theta_i/theta_pwm需核实；标称dt与实际周期差异保留 | [架构](ARCHITECTURE.md)、[003时序方案](specs/003-c-port-architecture.md) |
| 必要硬件参数 | 最高转速、实际电机/旋变极对数、允许电角误差、保护阈值/硬件链资料未定 | [003](specs/003-c-port-architecture.md)、[参考约束](reference/odrive-porting-guidelines-draft.md) |
| 命令管理 | mailbox、seq/epoch、STOP/到期/故障优先、确认及会话防重放尚未实施；HOST行为测试待实现后安排 | [003第6节](specs/003-c-port-architecture.md) |
| 通信、DMA和存储 | 真实首选接口/超时未选定，DMA池/MPU/cache合同未实施；CAN/ETH/RS485/外部Flash分功能推进 | [003第9节](specs/003-c-port-architecture.md) |
| 验证可追溯性 | 原AXF差异原因未知；已有重建不能解释旧差异。001板测不移作002；截图无板端固件指纹回读 | [003证据](validation/003-architecture-build.txt)、[002记录](validation/002-axis-status-bench.md) |

实现与验证分开：源码检查/编译/主机/板测各自判定。原始日志在validation，功能验收表在spec；不另建重复的验证总表。
