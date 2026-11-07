# 005 — 纯 C 整组命令交接

**一句话：** 后台一次提交完整的速度目标和 Iq 上限，由 TIM1 在控制边界接收；STOP、故障和到期撤销旧许可。

**状态：** 已实现；SRC/BUILD/HOST PASS；HW待机4项有限观察PASS，实际命令交接/输出/闭环未板测。基线 `9650972`，分支 `Xiao_ODrive`，开始时工作区干净。先写本 spec，再实现；用户已要求继续，无默认审批等待。

| 改哪里 | 改前 → 改后 | 目的 / 执行者 | 验证 |
| --- | --- | --- | --- |
| 命令核心及目标适配 | 独立全局量 → 32 B 请求、单槽、代次和确认 | appTask 校验发布，TIM1 消费 | 已实现；见下方分层验收 |
| 运行入口 | 调试变量可启动 → 只有有效活动命令可申请运行 | 隔离旁路，最终 board_safety 仍否决 | 已实现；见下方分层验收 |
| 停止/期限 | 请求可长期保留 → STOP 独立票据、活动命令到期 | 撤输出、作废旧代次，必须新 START | 已实现；见下方分层验收 |
| Iq 限制 | 仅慢环更新 → 每个快环限制最终 Iq 指令 | 新上限本拍生效 | 已实现；见下方分层验收 |
| 文档与测试 | 拟议合同 → 可执行 C 行为测试与目标构建证据 | 同一核心代码，不用模型代替实现 | 已实现；见下方分层验收 |

| 验收 | 结果 | 边界 |
| --- | --- | --- |
| SRC | PASS | 两路独立复核；目标对象反汇编确认PRIMASK保存/CPSID、发布调用、DMB、恢复；不能证明实际耗时 |
| BUILD | PASS | ARMCC5全量：0错误0警告，3分23秒；ROM61688 B，静态RAM25544 B；[原始证据](../validation/005-command-handoff.txt) |
| HOST | PASS | 同一核心8组203断言＋实际FOC/Xmath接入55断言；记录见[005证据](../validation/005-command-handoff.txt) |
| HW待机观察 | PASS（限定） | 本轮截图：许可0、输出标志0、ISR最大7462 cycles、超时0；时间未精确报告，见[追加记录](../validation/005-command-handoff.txt) |
| HW命令/输出/闭环 | NOT_RUN | 未实际发布命令；物理EN/PWM、STOP/到期、最坏IRQ/临界区仍待验证 |

**下一步：** 本组待机观察收尾，不再追加Watch；审阅并手动提交当前检查点 → 在本005内准备编译时强制禁功率输出的板上命令演练 → 后续明确真实配置/通信来源。测试模式尚未实现，不能通过写Watch启动当前固件。

## 范围与取舍

沿用 [003 第6节](003-c-port-architecture.md#6-纯-c-整组命令交接合同-v0拟议尚未实现) 的速度模式合同，独立 C 实现，不复制 ODrive C++。保留单业务任务、TIM1 IRQ0、ADC/旋变采集、现有 PI、DMA/时钟/CubeMX 配置。

工单载荷是八个 32 位字段（32 B），不是线路报文：`command_seq/run_epoch/opcode/control_mode/speed_target_rpm/iq_limit_a/received_tick_ms/expires_tick_ms`。仅速度模式 1，START/UPDATE；其他模式明确拒绝。板端分配接受序号；拒绝不消耗序号。槽满返回 BUSY，不覆盖。接收时间和原代次由调用方在请求进入时固定，不允许排队后换代/续期。没有接入真实 RX、会话握手或跨复位防重放。

实际速度、电流、命令有效期范围尚未确认。目标固件配置保持未确认，因此 START/UPDATE 返回 CONFIG_UNSET；主机正向测试的范围只是测试夹具。STOP 不受缺配置/满槽影响。旧 Watch 中 `foc_run_request`、模式、速度、电流限制及 step 不再是有效命令入口。此阶段不能通过旧调试路径启动电机。

## 同步与生效

- appTask 是唯一普通发布者。在局部副本上做有限数、模式、范围和期限校验；临界区内复核 fault、epoch、STOP/满槽，固定复制整组并发布。
- 目标适配保存 PRIMASK、关中断、DMB、恢复原值；不用 RTOS BASEPRI，不自旋，不分配，不在临界区解析/SPI/格式化。
- TIM1 优先级 0，独占 active 和运行代次。入口先取得反馈有效性（此时尚无控制计算），再处理 fault/STOP/活动到期，再消费至多一个请求，先于慢环/FOC；任务不能抢占。最终开输出前再检查许可与故障。
- 现有 board_safety 仍先撤 EN/MOE、锁存故障。命令状态在 TIM1 下一次边界/最终复核时失效；Fatal/NMI 不返回且不接触可能半写的邮箱。这是对 003 异步修改 epoch 方案的简化：避免两个上下文写 active，硬件关断不等代次更新。
- STOP 独立票据；pending 时重复请求返回相同票据。先撤输出再更新软件代次/确认；普通槽满不能挡 STOP。STOP 是撤使能，不是受控减速。
- 命令到期以 HAL/TIM7 毫秒为准，跨度小于 2^31 ms；pending 和 active 均检查。活动到期先撤输出、清 pending/active、推进代次一次，新 UPDATE 不得延长已经过期的运行。
- 命令序号和运行代次不静默回绕，耗尽后保持禁止；`control_seq`只是观测计数，允许uint32模回绕，不参与授权或期限；应用确认只代表软件接受，不代表电机已到目标或 PWM 已更新。
- 同一快环始终使用同一活动版本。速度斜坡仍在慢环；Iq 上限降低在本快环约束 Iq 指令及速度 PI 积分器，不是实际电流过流保护。

## 验证要求与风险

执行 A/B 整组、满槽、重复/乱序、START/UPDATE 状态、STOP 满槽优先/重复、故障晚于发布/先于输出、旧代次、pending/active 到期、HAL tick 回绕、半范围歧义、NaN/Inf、模式 0/2 拒绝、配置缺失、序号耗尽、非慢环拍降限流等测试。目标端固定复制实际屏蔽 IRQ 的时间、完整 IRQ 最坏耗时和物理输出仍须板测；主机不能证明 Cortex-M7 时序。

保护阈值/极对数/最高转速/物理采样对时未定，Break 仍关闭，原硬件验证缺项继续保留。新功能不新增清错或自动重启。RAM 使用静态 AXI 对象，不使用新 heap、DMA 或缓存维护。


## 实际文件与接口

- `Core/Inc/axis_command_core.h` / `Core/Src/axis_command_core.c`：平台无关C核心。`Validate`区外检查，`PublishValidated`在调用者锁内固定交接；`Tick/Guard/Revoke`只由TIM1执行。静态配置在Init固定，不提供运行中改限额接口。
- `Core/Inc/axis_command.h` / `Core/Src/axis_command.c`：目标适配；`Submit/Stop`检查当前确为appTask，`GetEpoch`读代次。`command_limits={0,0,0,0}`表示未确认且拒绝，不表示无限制；沒有调用这些发布入口的真实RX或自动任务。
- `Core/Src/app_runtime.c`：初始化命令模块；`BeginCycle`先于慢环和FOC，`FinalPermit`在既有最终许可临界区复核。运行条件丢失立即撤许可，恢复条件不会自动重启；采样故障仍先锁存关断。
- `Core/Src/open_foc.c`：`ApplyCommand`安装私有目标，旧镜像不再授权；移除原速度step和有隐藏±1000限幅的旧setter。`Stop`立即重置PI和启动沿、允许未来明确START；`EnforceCommandLimit`在慢环之后、电流PI之前限制三个Iq量。其他PI/方向/零位调试参数未纳入本合同，不允许并发调参。
- `Core/Inc/axis_status.h`：新增COMMAND_CONFIG=0x200、NO_COMMAND=0x400两项观测原因，状态字仍无运行权。准备好且无故障时预计`axis_status_word=0x00060002`，不再是002旧版本的0x2。
- `MDK-ARM/FOC_T.uvprojx`：增加两个C源；不改CubeMX、FreeRTOS、DMA、时钟或链接区域。
- `tests/run-command-host.ps1`：可复现HOST脚本；使用本机既有Quartus捆绑GCC4.5.3。`tests/host_stubs`只替代ARR/CCR和头文件，实际FOC/Xmath参加测试，未加入Keil目标。

接受与确认：Submit返回的seq是接受号，不是应用确认；核心保留`applied_seq/applied_control_seq`与`result_seq/result`。发生撤销时`cancelled_seq`标识被丢弃的已接受pending（若同时有active，result_seq指active）；`stop_applied_ticket`单独确认STOP。这些Watch镜像并非多字段原子快照/可靠ACK队列，真实协议接入前需补一致状态读取、事务映射和会话。请求首次进入时固定epoch和HAL时间，调用方不能给旧队列换代或续期。

## 本版本待机4项观察（已返回截图）

前提：仍是功率母线断开、仅控制与传感器供电；用户自行下载本次构建并运行约30秒。不要写入run、step或其他控制量。

| Watch | 预期 | 能说明什么 |
| --- | --- | --- |
| `axis_command_permit` | 0 | 未确认配置没有授予运行许可 |
| `board_output_enabled` | 0 | 软件输出标志关闭；不代替物理EN/MOE测量 |
| `foc_isr_max_cycles` | 大于0且小于9996 | 已执行的停止路径函数内最大耗时低于当前周期预算；不代表完整IRQ/WCET |
| `foc_isr_overrun_count` | 0 | 本观察内未记到该函数超预算 |

本组够用于判断是否可继续下一软件步骤，不追加旧002整套参数。发布尚无真实调用，`axis_command_critical_max_cycles=0`不能作为发布耗时验收。真实STOP/命令跨中断发布、完整屏蔽时长、实际PWM/EN和闭环仍未板测。

## 验证边界

HOST只有顺序API和寄存器替身，不证明真实抢占、D-cache、DMA或物理电流。Iq限制是指令限幅，对齐电压/Id和实际相电流保护不在它的保证中。配置未确认、会话机制未实现、采样物理对应及保护链未验证等限制保留；不把旧板测移作新固件验收。

目标构建结果：ROM比上一阶段增加1876 B，静态RAM增加200 B；RTOS堆/任务栈不变。链接器会移除无调用的Submit/Stop/GetEpoch及对应发布/校验段，它们已编译并作对象反汇编核对；真正协议调用接入时会进入最终映像并需重新测时，当前固件不执行这些发布路径。


## 2026-11-07 待机观察结果与下一步

![005待机Watch](../validation/images/005-idle-watch-20261107.png)

用户返回单张截图：`axis_command_permit=0`、`board_output_enabled=0`、`foc_isr_max_cycles=0x1D26=7462`、`foc_isr_overrun_count=0`。按配置200 MHz折算37.31 μs，小于9996 cycles/49.98 μs。只代表这次已观察的停止路径，不能称为闭环最坏耗时或物理保护验证；具体运行时长未报告。

原29项指纹中27项相同。`board_safety.c`只新增空行；用户增量编译0错误0警告，AXF变化，但HEX和MAP与原记录完全相同。原构建记录保留，最新指纹与截图追加到[同一证据文件](../validation/005-command-handoff.txt)，板端产物未回读。当前代码未在本次答复中修改。

本组足够结束待机观察并保存提交。下一段仍属于005命令交接的验证：准备一个编译时强制禁功率输出的测试模式，让appTask真正发布完整命令，TIM1消费，再验证STOP和到期撤销。它需要把测试许可与物理输出禁止明确分开，并在编译期约束，不能只凭Watch中的输出0就认为隔离成立。测试范围/参数仅供演练，生产配置继续保持未确认；本次只记方案，不启用测试模式。板测安排仍每轮3–4个关键量。
