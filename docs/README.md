# FOC_T 项目入口

**现在：** RTOS与002待机观察已提交。005整组命令入口已接入：appTask发布、TIM1接收，STOP/故障/到期撤销；实际速度/电流/期限范围未确认，固件拒绝运行命令。没有真实通信接收或DMA。

**当前功能：** [005整组命令交接](specs/005-command-handoff.md)。**接下来：** 005待机4项有限观察已收尾，先审阅并手动提交检查点，再在同一005内准备强制禁输出的板上命令演练；正式运行配置与真实协议另行推进。

**日常先看本页，再看当前spec。** 当前分工/数据/输出权限统一查 [ARCHITECTURE.md](ARCHITECTURE.md)。

## 当前版本与最新变化

- 分支Xiao_ODrive，已提交基线`9650972`，本轮为其上的005未提交实现；代码、配置和产物指纹见005验证记录。
- 速度/Iq限制改为整组提交，旧Watch的run/step/目标/限制/请求模式不再是运行命令入口；START只支持速度模式1。
- 缺配置时明确拒绝运行；STOP不受缺配置或满槽阻挡。软件命令接入不代表真实通信、物理保护或闭环验收。
- 005本轮待机截图符合4项预期；单截图不证明准确运行时长或实际命令交接。用户增量构建HEX与原记录一致；最新AXF/空行差异已追加记录。002旧板测和003旧AXF差异记录继续保留。

## 文档与证据怎么找

| 想看什么 | 唯一入口 |
| --- | --- |
| 当前实现：谁采样、谁计算、谁能开输出 | [当前架构](ARCHITECTURE.md) |
| 当前改动、验收、下一组关键参数 | [005 spec](specs/005-command-handoff.md) |
| 当前构建/主机测试与指纹 | [005验证记录](validation/005-command-handoff.txt) |
| 上一阶段原始截图及有限观察 | [002台架记录](validation/002-axis-status-bench.md) |
| 上一阶段构建与AXF差异 | [003构建记录](validation/003-architecture-build.txt) |
| 命令方案来源、后续时序/DMA | [003方案](specs/003-c-port-architecture.md)；已实现部分以005和架构页为准 |
| 文档职责、RTOS/Git历史 | [004](specs/004-docs-organization.md)、[001](specs/001-rtos-preflight.md)、[000](specs/000-git-workflow-demo.md) |
| 长期约束与ODrive参考 | [参考稿](reference/odrive-porting-guidelines-draft.md) |

## 未关闭事项

| 事项 | 下一步 / 证据边界 | 回链 |
| --- | --- | --- |
| 005新增固件板测 | 待机4项有限观察已收尾；板上命令交接/STOP/到期、发布临界区、物理输出、IRQ最坏耗时仍待验证 | [005](specs/005-command-handoff.md) |
| 完整时序与资源 | 停止路径不代表闭环最坏耗时；MSP、嵌套IRQ、完整关中断时长、最坏任务栈仍待验证 | [001](specs/001-rtos-preflight.md)、[架构](ARCHITECTURE.md) |
| 实际输出与保护 | EN/MOE/PWM、死区、真实关断延迟、故障注入及运行态未验收；Break关闭，AD2S器件故障仍仅诊断 | [001](specs/001-rtos-preflight.md)、[002](specs/002-axis-status-observer.md) |
| 电流和角度对应 | ADC源帧/时刻、量程/极性、角度零位与theta_i/theta_pwm待核实；标称dt与实际周期差异保留 | [架构](ARCHITECTURE.md)、[003](specs/003-c-port-architecture.md) |
| 必要硬件及运行配置 | 最高转速、实际电机/旋变极对数、角误差、Iq/速度范围、期限和保护阈值未定；不以HOST夹具代填 | [005](specs/005-command-handoff.md)、[参考约束](reference/odrive-porting-guidelines-draft.md) |
| 通信、DMA和存储 | 无实际请求生产者/协议/一致ACK快照/跨复位会话；DMA池/MPU/cache合同、CAN/ETH/RS485/Flash分功能推进 | [003](specs/003-c-port-architecture.md)、[005](specs/005-command-handoff.md) |
| 原有证据边界 | 旧AXF差异原因未知；截图无板端固件指纹回读；cache实际状态与完整采样间隔待测 | [003证据](validation/003-architecture-build.txt)、[002记录](validation/002-axis-status-bench.md) |

实现与验证分开：源码/编译/主机/板测分别判定。原始证据在validation，功能验收表在spec。
