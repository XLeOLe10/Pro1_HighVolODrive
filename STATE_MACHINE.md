# 单电机状态机移植说明

本次在 `state` 分支将 ODrive Axis 的请求消费、校准任务衔接、进入条件和失败退出机制改写为 C 状态机。只管理一个电机，不依赖 RTOS、C++、Fibre 或双轴对象。

参考本地 `D:/ODrive-master/Firmware/MotorControl/axis.cpp` 的 `run_state_machine_loop()`、`start_closed_loop_control()`，以及 `main.cpp` 的实时检查机制。参考目录不是 Git 仓库，无法从本地确定提交号。本次保留原工程的对齐和 PI 算法；没有复制 ODrive 的 R/L 辨识、编码器扫描或无感算法。

## 状态与模式

`foc_axis_state` 只读观察当前运行阶段；向 `foc_axis_requested_state` 写入请求，下一次 1 ms 调度消费并恢复为 0。不要直接修改当前状态和输出使能变量。

| 数值 | 状态 | 作用 |
| --- | --- | --- |
| 0 | UNDEFINED | 请求邮箱为空，不是运行状态 |
| 1 | IDLE | 停止，关闭功率级 |
| 2 | ENCODER_OFFSET_CALIBRATION | 固定电角度对齐 1200 ms，完成后更新旋变偏移 |
| 3 | CLOSED_LOOP_CONTROL | 执行所选电流/速度闭环 |
| 4 | OPEN_LOOP_TEST | 电压开环旋转磁场，观察旋变与极对数估计 |
| 5 | FAULT | 错误锁存、禁止功率输出，不接受运行请求 |
| 6 | STARTUP_SEQUENCE | 仅为请求：按当前模式执行启动流程 |
| 7 | INITIALIZING | 等待现有三相电流零偏采集完成 |

控制模式仍使用原有 `foc_control_state_request/active`：0 电流环、1 速度电流双闭环、2 开环测试。这些数值不是 ODrive 协议状态编号，不能直接接入 ODrive CAN 协议。

启动链为：`INITIALIZING → IDLE → 对齐 → CLOSED_LOOP_CONTROL`。开环测试跳过对齐。单独请求状态 2 时，校准完成回 IDLE；启动请求中的对齐完成后进入闭环。新状态请求可以中止当前任务。

保留 `foc_run_request` 的 Watch 操作：置 1 启动所选模式，置 0 停止。显式状态请求在 1 ms 调度时执行，快环中置 0 的运行标志会优先撤销输出。故障退出同时清除运行请求，禁止自动重启。

## Keil Watch 操作

1. 上电等待 `CurrentMonitor_IsReady()` 成立，`foc_axis_state` 变为 1。初始化过程中发出的启动请求会保留至电流零偏采集完成。
2. 设置 `foc_control_state_request = 0` 或 `1`，再置 `foc_run_request = 1`。也可向 `foc_axis_requested_state` 写入 6 或 3。闭环要求旋变位置数据和健康状态都有效。
3. 对齐期间 `foc_aligning = 1`。只有实际功率级已使能时才累计 1200 ms；功率级连续 2000 ms 未使能会报错。对齐电压、时间沿用旧工程，不是电机参数辨识。
4. `foc_auto_calibrate_offset = 1` 时更新 `foc_theta_offset_rad`；为 0 时保留手工偏移，但启动仍执行对齐。单独请求状态 2 会强制更新偏移。
5. 电流环与速度环互切会清积分、初始化速度斜坡，不重复对齐。开环切回闭环必须重新通过对齐。
6. 开环测试可设置模式 2 再运行，或请求状态 4。它有意不依赖旋变反馈，但仍要求电流采样就绪；速度与电压继续使用原调试参数。
7. 停止：`foc_run_request = 0`，或请求状态 1。`OpenFOC_Stop()` 也可停止，随后允许再次发出运行请求。
8. 故障恢复：先排除故障，保持 `foc_run_request = 0`，置 `foc_axis_clear_errors = 1`。电流采样须已恢复；旋变相关故障还要求旋变恢复。清错成功回 IDLE，必须再次启动。同一次清错中的启动请求会被丢弃。

`foc_axis_offset_valid` 表示本次对齐流程完成或采用手工偏移，并不证明极对数、方向或机械安装参数正确；没有 Flash 持久化。

## 错误位

| 位值 | 含义 |
| --- | --- |
| 0x01 | 非法请求状态或控制模式 |
| 0x02 | 输出期间电流反馈失效 |
| 0x04 | 闭环/对齐时旋变反馈失效，或启动时旋变未就绪 |
| 0x08 | 功率级未使能超时 |

`foc_axis_error` 为锁存错误位。20 kHz 路径每周期检查电流有效性，闭环和对齐还检查旋变有效性；异常时撤销输出、清除运行请求并进入 FAULT。AD2S1210 的健康标志由已有慢速故障采集更新，因此旋变芯片故障检测延迟还包含该采集周期。

本次没有新增母线电压、相电流阈值、温度或驱动器故障引脚采样，也没有通信看门狗。`CurrentMonitor_IsReady()` 主要表示零偏采集完成，不能代替硬件过流保护。

## 调度与功率输出

- `axis_state.c/.h`：单轴状态、任务后继、错误锁存和校准计时。
- `open_foc.c`：根据状态选择固定角度对齐/闭环/开环；状态或模式改变时重置 PI 和参考值。20 kHz 电流 PI 与 1 kHz 速度 PI 参数不变。
- `main.c`：TIM1 ISR 独占运行期间的功率级启停。先计算占空比，再连接 PWM 引脚并使能 PB10；停止时关闭 PB10 并将六路 PWM 引脚恢复为低电平 GPIO。TIM1 和内部 CH4 触发保持运行，以便待机采样和再次启动。
- 默认板级配置仍为 PB10 高有效。驱动器无 EN 的配置通过 PWM 引脚隔离停止输出；尚未进行该配置的硬件验证。

状态管理是非阻塞的，不在 ISR 中使用 `HAL_Delay` 或轮询等待。功率级 GPIO 切换仅发生在启停边沿，仍需上板观察 `foc_isr_max_cycles` 与时序。

## 验证

已使用工程配置的 ARMCC 5.06 update 7 构建 Keil 目标 `FOC_T`，生成 AXF/HEX；编译结果为 0 错误、0 警告。

主机回归使用真实 `axis_state.c`、`open_foc.c` 与 `Xmath.c`，只模拟 TIM1 寄存器和传感器输入。安装 GCC 后在工程目录运行：

```powershell
.\Tests\run_state_tests.ps1
```

覆盖启动等待、1200 ms 对齐、独立校准、模式切换、反馈故障快速退出、故障清除不自启动、非法请求、停止再启动、功率级等待与超时。主机测试不能验证 MOS 管波形、采样同步、旋变信号和电机实际响应；本次未烧录或驱动电机。
