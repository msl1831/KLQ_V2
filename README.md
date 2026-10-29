# KLQ V2

APM32E103RET6 编程机器人项目。项目远程仓库为 https://github.com/msl1831/KLQ_V2 ，后续源码与文档在此同步。

- [功能需求 V0.9](KLQ功能需求文档_V0.5.md)：整体功能、PC4、RAM程序规则及外接固件升级。
- [当前机器人架构](docs/architecture_current.md)：现有硬件、固件分区、启动流程、通信和下一阶段应用分层。
- [当前开发交接](docs/handoff_current.md)：主控分支、最新板上版本、测试结果与后续工作。
- [主控0.9外接升级验证](docs/verification_20261029_host_upgrade.md)：最新Python业务与USB→UART升级的真实合流、资源、工具和交付。
- [主控0.9 USB安装](docs/verification_20261029_usb_install.md)：通过主机自身USB完成安装、CRC提交、启动及版本/回显回读，原引导0.3保留。
- [红外与超声实测](docs/verification_20261029_sensors_live.md)：P1测距/P2左右响应、错误增量，以及一次非人为IR掉线/重启迹象的待定位记录。
- [构建环境与依赖](docs/development_setup.md)：主控、三类PY32、固定SDK恢复及重复验证命令。
- [传感器数据访问决策](docs/decisions/001_sensor_access_strategy.md)：后台采集、静态快照、数据新鲜度和后续 Python API 约束。
- [精简 Python 与积木接口 V0.1](docs/python_api_v0.1.md)：上位机代码生成规则、26 个非语音积木映射、枚举和错误码。
- [上位机 USB 通信协议 V0.1](docs/host_usb_protocol_v0.1.md)：KLQ1 帧格式、Python 源码下载、运行控制与状态查询。
- [USB 引导说明](README_BOOTLOADER.md)：构建、首次 DAPLink 烧录、系统固件下载协议和内部 Flash 分区要求。
- [最终验证记录](docs/verification_20260914.md)：实测结果、固件校验值和测试范围。
- [SC7A20HTR 与串口验证](docs/verification_20260914_sc7_uart.md)：本次传感器、角度输出及五路串口实测。
- [精简 Python 首版验证](docs/verification_20260921_python_runtime.md)：固件资源、构建结果、实板验收步骤和当前边界。
- [PC4 短按实板验证](docs/verification_20260923_power_key.md)：RAM 程序的实体键运行／停止和计数观测。
- [长按开关机与通信回归](docs/verification_20260923_power_on_off.md)：1.5 秒按键门槛、USB 引导、运行与显示实板验证。
- [RAM 程序与解释器容错验证](docs/verification_20260923_runtime_faults.md)：错误退出、无延时循环停止、运行中下载替换及固件资源。
- [26 个非语音积木测试](docs/verification_20261014_blocks.md)：真实 C 模拟、实板接口矩阵、执行命令及物理验收边界。
- [显示与 RAM 用户程序验证](docs/verification_20260915_ui_ram.md)：自动启动、显示状态机、RAM 下载和引导升级回归。
- [外接端口与 CS100A 验证](docs/verification_20260915_external_ports.md)：UART1～3 自动发现、连续测距和掉线重连实测。
- `bootloader/`：电源保持、7×13 TM1640 点阵、USB CDC 与机器人系统固件更新。
- `tools/`：构建、烧录和验证脚本。
- `vendor/APM32E10x_EVAL_SDK-main/Libraries/`：构建所需的 Geehy SDK 库，保留上游声明和许可证。
- [首次成功版本对照](diagnostics/usb_first_success/README.md)：保留的历史诊断源码，可独立构建；当前板子运行修复后的主线引导。

**当前固件0.9：保留精简Python业务，合入USB→UART外接固件升级。** PC4长按1.5秒开/关机、短按运行/停止，用户程序只驻留16 KiB RAM；支持显示、PC3、SC7、通用端口发现和CS100A距离等待。新增双路IR端口采样与UL/IR/PY32电机升级桥接，已完成构建和软件回归。2026-10-29另经主机自身USB安装0.9并核对CRC、版本、待机和ECHO，外接升级仍待实板验收。26积木生成接口保持不变；Python电机控制/IR条件仍待主控驱动接入，不能把模拟成功当成物理动作通过。

构建依赖 Keil ARM Compiler 5；Python 工具依赖见 `tools/requirements.txt`。生成文件、本机虚拟环境及板载固件备份不提交到仓库。完整操作见引导说明。

实时观测页面使用机器人 USB CDC 的文本日志和 KLQ1 INFO 响应，同时显示三个外接端口、SC7A20HTR 与系统状态：

```powershell
python tools/status_dashboard.py --port COM123
```

启动后访问 `http://127.0.0.1:8765`，待机表情候选在 `http://127.0.0.1:8765/faces`。观测页面每 250 ms 刷新，串口断开后自动重连；同一时间不要再运行其他占用 COM123 的串口工具。

第三方 SDK 按其自身许可证使用，本仓库未为整个工程另行指定统一开源许可证。
