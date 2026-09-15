# KLQ V2

APM32E103RET6 编程机器人项目。项目远程仓库为 https://github.com/msl1831/KLQ_V2 ，后续源码与文档在此同步。

- [功能需求 V0.5](KLQ功能需求文档_V0.5.md)：整体功能、显示状态规则、用户程序 RAM 驻留规则及已确认的硬件分配。
- [当前机器人架构](docs/architecture_current.md)：现有硬件、固件分区、启动流程、通信和下一阶段应用分层。
- [传感器数据访问决策](docs/decisions/001_sensor_access_strategy.md)：后台采集、静态快照、数据新鲜度和后续 Python API 约束。
- [USB 引导说明](README_BOOTLOADER.md)：构建、首次 DAPLink 烧录、系统固件下载协议和内部 Flash 分区要求。
- [最终验证记录](docs/verification_20260914.md)：实测结果、固件校验值和测试范围。
- [SC7A20HTR 与串口验证](docs/verification_20260914_sc7_uart.md)：本次传感器、角度输出及五路串口实测。
- [显示与 RAM 用户程序验证](docs/verification_20260915_ui_ram.md)：自动启动、显示状态机、RAM 下载和引导升级回归。
- [外接端口与 CS100A 验证](docs/verification_20260915_external_ports.md)：UART1～3 自动发现、连续测距和掉线重连实测。
- `bootloader/`：电源保持、7×13 TM1640 点阵、USB CDC 与机器人系统固件更新。
- `tools/`：构建、烧录和验证脚本。
- `vendor/APM32E10x_EVAL_SDK-main/Libraries/`：构建所需的 Geehy SDK 库，保留上游声明和许可证。
- [首次成功版本对照](diagnostics/usb_first_success/README.md)：保留的历史诊断源码，可独立构建；当前板子运行修复后的主线引导。

**当前状态：V0.2 引导和机器人系统固件已烧录，机器人 USB 为 COM123。** 有效系统固件在上电或普通复位后自动启动，播放开机动画并进入待机；显式 USB `reset` 可一次性停留在引导。系统固件已经实现 16 KiB RAM 用户程序下载、显示状态机，以及 UART1～3 任意端口 KLQ1 自动发现。当前 CS100A 在物理端口 1 完成 INFO、连续 GET_SAMPLE 和掉线重连实测。精简 Python 解释器尚未接入，因此当前“运行”命令只验证状态与显示，不执行 Python 代码。完整范围见验证记录。

构建依赖 Keil ARM Compiler 5；Python 工具依赖见 `tools/requirements.txt`。生成文件、本机虚拟环境及板载固件备份不提交到仓库。完整操作见引导说明。

实时观测页面使用机器人 USB CDC 的文本日志和 KLQ1 INFO 响应，同时显示三个外接端口、SC7A20HTR 与系统状态：

```powershell
python tools/status_dashboard.py --port COM123
```

启动后访问 `http://127.0.0.1:8765`。页面每 250 ms 刷新，串口断开后自动重连；同一时间不要再运行其他占用 COM123 的串口工具。

第三方 SDK 按其自身许可证使用，本仓库未为整个工程另行指定统一开源许可证。
