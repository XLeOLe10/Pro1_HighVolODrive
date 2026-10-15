# Pro1_HighVolODrive

高压风扇 STM32H750 FOC 控制工程。先保存现有双闭环和状态控制程序作为第一版，再逐步参考、移植 ODrive 的控制模块。

## 第一版基线

- 基线标签：`v1.0.0`。
- 首次纳入 Git 的日期：2026-10-15；此日期表示归档时间。
- 主控：`STM32H750VBTx`（工程配置）。
- Keil 工程：`MDK-ARM/FOC_T.uvprojx`，目标名 `FOC_T`。
- 编译器：Arm Compiler 5.06 update 7，build 960。
- 外部设备包：`Keil.STM32H7xx_DFP.4.1.3`。
- 现有构建记录实际使用 `ARM.CMSIS 5.9.0`（CORE 5.6.0），而工程原始 RTE 声明仍为 CMSIS 4.5.0 / CORE 4.3.0。本次保留原配置；换电脑时需安装匹配的编译器和软件包，并单独验证完整重建。
- 电流环周期：50 us（20 kHz）；速度环周期：1 ms（1 kHz）。
- 位置/速度反馈：AD2S1210 旋变接口。
- 本次归档保留现有固件源文件及工程配置，没有移植 ODrive 代码。

`Core/Inc/open_foc.h` 定义现有三种控制状态：

| 数值 | 枚举 | 用途 |
| --- | --- | --- |
| 0 | `FOC_CONTROL_CURRENT_LOOP` | 电流环调试 |
| 1 | `FOC_CONTROL_SPEED_CURRENT_LOOP` | 速度外环 + 电流内环 |
| 2 | `FOC_CONTROL_RESOLVER_OPEN_LOOP_TEST` | 旋变 / 极对数开环调试 |

运行请求为 `foc_run_request`，请求状态为 `foc_control_state_request`，当前状态为 `foc_control_state_active`。PI 参数和调试量见 `Core/Src/open_foc.c`。

## 目录

- `Core/Src`、`Core/Inc`：控制、状态切换、采样、通信与外设代码。
- `Core/math`：数学变换。
- `Drivers`：工程本地的 HAL / CMSIS 驱动及其许可证。
- `MDK-ARM`：Keil 工程、启动汇编和调试配置。
- `FOC_T.ioc`、`.mxproject`：CubeMX 配置。
- `PORTING_H750.md`：此前 G474 到 H750 的移植记录，部分描述对应当时的开环调试阶段；当前控制行为和参数以源码为准。

编译产物、历史日志和个人 Keil 窗口状态由 `.gitignore` 排除。工程文件、启动文件、驱动库和调试配置纳入版本管理。`.gitattributes` 保留既有固件文件的编码和换行字节。

## 验证范围与现有行为

归档前已核对 Keil 工程引用的 39 个源文件：均存在，并使用工程目录内的相对路径；编译器和软件包仍为外部依赖。本次 Git 初始化没有重新编译、烧录或运行电机。上板运行结论沿用当前已完成的人工调试。

当前仍是调试版本：`main.c` 中 AD2S1210 的状态和故障按诊断信息处理，不会因此停止 PWM 或清除运行请求。第一版标签保存的是这一现有行为。运行时通过 Keil Watch 临时修改的参数不一定写入源码；需要复现的调参值应另行记录并提交。

## 第一次保存版本的流程

1. 确认当前实际编译的工程目录。
2. 初始化本地 Git：`git init -b main`。
3. 添加忽略规则和项目说明，检查待收录文件。
4. 暂存：`git add .`；暂存表示选定下一次提交包含的内容。
5. 提交：`git commit -m "chore: save v1 FOC baseline"`；提交创建本地快照。
6. 打标签：`git tag -a v1.0.0 -m "V1: current loop, speed-current loop and control states"`。
7. 连接远程：`git remote add origin https://github.com/XLeOLe10/Pro1_HighVolODrive.git`。
8. 推送主分支和标签：`git push -u origin main`，然后 `git push origin v1.0.0`。

Git、提交和标签在本地即可使用。推送才把这些版本上传到 GitHub。

## 后续在 VS Code 中操作

用 VS Code 打开包含本 README 和 `.git` 的 `FOC_T` 根目录，按 `Ctrl+Shift+G` 打开源代码管理。

- 修改并保存文件后，点击文件查看差异。
- 确认本次修改范围，点击 `+` 暂存。
- 输入说明并提交，完成一个便于复查的本地版本。
- 使用“推送”上传提交；如果提示“同步”，先确认是否存在远程新提交。

开始 ODrive 移植前，从第一版创建开发分支：

```powershell
git switch -c codex/odrive-integration
git push -u origin codex/odrive-integration
```

`main` 保留已验证的版本，开发分支承载移植工作。每个模块分别提交并验证，通过后再合并回 `main`。

需要查看第一版时，先保存当前修改，再运行：

```powershell
git switch -c review/v1.0.0 v1.0.0
```

这会从第一版新建检查分支。查看完成后可用 `git switch codex/odrive-integration` 返回开发分支。

## 下一步：ODrive 参考与移植

1. 固定 ODrive 参考仓库的 tag / commit，记录来源及其许可证。
2. 对照现有状态控制、速度环和电流环，确定第一个移植模块及验证指标。
3. 每次只引入一个可验证的模块，记录修改前后的结果。
4. 验证通过后提交，再开始下一个模块。
