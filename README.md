# KLQ V2

APM32E103RET6 编程机器人项目。项目远程仓库为 https://github.com/msl1831/KLQ_V2 ，后续源码与文档在此同步。

- [功能需求 V0.2](KLQ功能需求文档_V0.2.md)：整体功能及已确认的硬件分配。
- [USB 引导说明](README_BOOTLOADER.md)：构建、首次 DAPLink 烧录、USB 下载协议和应用分区要求。
- [当前联调记录](docs/bringup_20260914.md)：实测结果和未解决问题。
- `bootloader/`：电源保持、7×13 TM1640 点阵、USB CDC 与应用固件更新。
- `tools/`：构建、烧录和验证脚本。
- `vendor/APM32E10x_EVAL_SDK-main/Libraries/`：构建所需的 Geehy SDK 库，保留上游声明和许可证。

**当前状态：引导可通过 DAPLink 烧录，点阵图标和方向已由用户确认；USB 曾完成通信和一次应用下载，但之后无法重新枚举，冷启动也未恢复，尚未验收。** 不应将当前版本视为已验证可用的 USB 下载引导。

构建依赖 Keil ARM Compiler 5；Python 工具依赖见 `tools/requirements.txt`。生成文件、本机虚拟环境及板载固件备份不提交到仓库。完整操作见引导说明。

第三方 SDK 按其自身许可证使用，本仓库未为整个工程另行指定统一开源许可证。
