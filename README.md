# KLQ V2

APM32E103RET6 编程机器人项目。项目远程仓库为 https://github.com/msl1831/KLQ_V2 ，后续源码与文档在此同步。

- [功能需求 V0.4](KLQ功能需求文档_V0.4.md)：整体功能、用户程序 RAM 驻留规则及已确认的硬件分配。
- [当前机器人架构](docs/architecture_current.md)：现有硬件、固件分区、启动流程、通信和下一阶段应用分层。
- [USB 引导说明](README_BOOTLOADER.md)：构建、首次 DAPLink 烧录、系统固件下载协议和内部 Flash 分区要求。
- [最终验证记录](docs/verification_20260914.md)：实测结果、固件校验值和测试范围。
- [SC7A20HTR 与串口验证](docs/verification_20260914_sc7_uart.md)：本次传感器、角度输出及五路串口实测。
- `bootloader/`：电源保持、7×13 TM1640 点阵、USB CDC 与机器人系统固件更新。
- `tools/`：构建、烧录和验证脚本。
- `vendor/APM32E10x_EVAL_SDK-main/Libraries/`：构建所需的 Geehy SDK 库，保留上游声明和许可证。
- [首次成功版本对照](diagnostics/usb_first_success/README.md)：保留的历史诊断源码，可独立构建；当前板子运行修复后的主线引导。

**当前状态：修复后的引导已烧录，机器人 USB 为 COM123。** USB 通信、系统固件下载/CRC 校验、中断更新保护、系统固件返回引导，以及连续 5 轮运行/返回均通过实机测试。当前系统固件演示镜像已加入 SC7A20HTR 三轴/角度输出和五路 UART 初始化，并完成实板验证。Python 用户程序将只驻留 RAM，相关下载与解释执行尚未实现。点阵图标和方向此前已由用户确认。完整范围见验证记录。

构建依赖 Keil ARM Compiler 5；Python 工具依赖见 `tools/requirements.txt`。生成文件、本机虚拟环境及板载固件备份不提交到仓库。完整操作见引导说明。

第三方 SDK 按其自身许可证使用，本仓库未为整个工程另行指定统一开源许可证。
