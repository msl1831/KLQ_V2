# 2026-10-29 主控升级桥接合流工作记录

代码、构建、模拟与交付整理已完成；本轮未访问设备或烧录。最终分支提交以git log为准，不能以起始基线代替交付HEAD。

- 授权：sensor boot 对话转达用户要求，在本对话维护的最新主控业务基线上集成 USB→UART 外接固件升级；只做代码、构建、模拟、交付，不自动烧录，不回写共享 main。
- 权威基线：`codex/python-runtime@ff33f97438f782a5844bf3ce6e2cd9ed120a402c`，工作树 `C:/Users/Administrator/.codex/worktrees/python-runtime/Codex`，开始时干净。系统固件0.8；共享 `F:/work/KLQ_V2/Codex` 是 `main@63d1ea3` 加未提交的桥接/IR/三类板端/语音等工作。
- 已读共享 handoff 14/15节、development_setup 8/9节、sensor_boot 协议/桥接/验证、UL/IR/MOTOR 三份 20261029 核对。内层协议与新电机身份 hw2 以这些当前资料为准，不沿用早期 HK32 hw1。
- 原始共享文件及业务文件、原始哈希、统一换行后的三方预览保存在 `F:/work/KLQ_V2/Codex/tmp/host_bridge_integration_20261029/{incoming,target,base,preview}`；154个外接组件第一方输入的快照/哈希在同目录 `components`、`component_inputs.json`。完整 ff33f97 git archive 和独立可构建导出在 `ff33f97.zip`、`authority_baseline`。
- 已按三方差异合并 external_ports.c/.h、protocol.c/.h、usb_serial.c、tools/build.py、dashboard；main.c 只针对当前业务版接入桥接日志门禁及 IR 日志，未带入旧 main 的语音试验代码。保留 16 KiB RAM、解释器、26积木、PC4电源、50/25Hz采样、快照时间戳、3ms日志发送。当前新构建应用标识0.9、引导回归标识0.4。
- 额外合流约束：user_program_start 在升级独占时拒绝启动（堵住实体短按绕过 USB RUN 门禁）；长按关机前取消会话；USB发送等待仍推进 power_key_poll 和升级UART；USB reset 取消升级及半包RAM下载，但保留完整RAM程序。升级关闭不自动重跑旧程序。
- 为使升级工具和测试可恢复，在本分支首次导入 sensor_boot、UL、IR、MOTOR 第一方源码/脚本/原始IO及手册，排除原有生成目录、虚拟环境、nested vendor 和 .git。未改三类板端生产源码。固定SDK依赖位于 sensor_boot/dependencies.json，提交 `394627d99085c5eae72142503b0405b84428bb20`，当前用共享工作区现有干净SDK只读构建。恢复方法待写入本分支 development_setup。
- 初次主控桥接native编译因旧VC98没有stdbool.h失败（main原头文件没有bool；新版头文件有），已在测试mock加C/C++兼容shim，不改固件类型。
- 主控0.9应用已构建：BIN24176 B；独立ff33f97应用重建20412 B。引导/最终map/HEX范围及新的资源证据待汇总。
- 三类镜像已从导入的当前源码、固定SDK重建，未使用旧主控ZIP：UL boot4464/app5776 CRC CF112888；IR boot4468/app6052 CRC 3CB2BDEC；Motor boot4456/app2880 CRC 0897C604。此Motor已包含其核对小修复，不能拿更早2856B副本覆盖。

## 完成结果

- ARMCC重建当前应用/引导及独立导出的基线；应用BIN24176/RO23884/RAM26784 B，引导BIN9944/RO9740/RAM8744 B，增量分别3764/336及100/8 B。恢复日志原160 B缓冲，字段上限证明CS/IR最大138/127 B，无需扩大栈。
- 端口161、USB300、真实业务7模块C共存176断言通过；Python30/30无跳过，4个整链路用例覆盖三类板×三个端口9次整包传输。该模拟终点为引导END，不执行ARM跳转。IR1190/107/104、UL pending reset及Motor安全/内核/应用通过。新增测试只用mock SDK类型，未改变生产C以适配旧VC98。
- 按本对话“继续完成其它积木模拟”要求复跑真实解释器/runtime，22组164175断言、26/26映射及7项工具异常清理通过；报告存docs/blocks_sim_20261029.json。电机动作成功依赖mock，IR仍覆盖不支持路径，未表述成全物理能力完成。
- 新packager核验所有向量/完整HEX/BIN/板型身份/CRC/manifest，生成23项129839 B的0.9包，SHA-256 a3a6e9d7a9fb4bb6d9334b050da3c3a221b2ab2949081be6b0190dafa5422237；两次生成同哈希，独立解包核验及电脑工具帮助入口通过。完整主控源哈希、各板manifest和资源在host_upgrade_build_evidence_20261029.json。
- 已完成README/需求V0.9/架构/API/USB协议/handoff/setup及verification_20261029_host_upgrade.md同步。外接SDK按dependencies.json固定来源，未导入.git或虚拟环境；三类44个生产文件与来源一致，共享原8个桥接/业务文件原始哈希保持。

后续仅剩另行授权的实板验收、Python电机/IR业务接入，以及共用板端引导RUN发送/TC超时保证。当前不宣称硬件时序/Flash/冷启动/物理停车通过；本轮未向其它对话发回消息。
