# 主控开发与外接升级构建环境（0.14，2026-10-30）

当前主机0.14已安装，驻留引导0.3保留。0.14应用32980 B/RAM27888 B，另有24B顶端Fault保留区；以下0.9/0.12制品路径为历史，依赖/SDK恢复规则继续适用。当前构建与新增回归可用：

```powershell
python tools/build.py --demo --output-dir build/host_0_14/app
python tools/test_usb_endpoint_register.py
python tools/test_status_dashboard_connection.py
python tools/test_python_motor_faults.py
python tools/test_tm1640_cache.py
python tools/test_system_guard.py
& sensor_boot/tests/run_port_freshness.ps1
```

端点模型默认VC98，支持--compiler；不接硬件，含两个必须失败的负对照。语音相关native回归为tools/run_voice_wire_tests.ps1、tools/run_voice_policy_tests.ps1、sensor_boot/tests/run_motor_business.ps1；新增USB工程协议见host_voice_protocol_v0.1.md。0.14优化与实板范围先看optimization_014.md；此前语音验收看verification_20261030_voice.md，勿将0.11/0.12和历史0.9结果混淆。


使用 `codex/python-runtime`，先读 `handoff_current.md` 并核对分支/提交/未提交内容。共享旧main包含语音等本地工作，本分支不是其整目录覆盖副本。

主控采用仓库内裁剪Geehy SDK、ARM Compiler 5.06 update6 build750，默认 `C:/Keil_v5/ARM/ARMCC/bin`，可用 `KLQ_ARMCC` 指定。Python使用3.12，USB工具依赖pyserial3.5；安装工具依赖见 `tools/requirements.txt`。本机native测试使用VC98 12.00.8168及专用stdint/stdbool mock，编译器路径可由各测试脚本 `-Compiler` 指定；这些mock不参与ARM目标构建。

## 主控构建

在本分支仓库根目录执行：

```powershell
python tools/build.py --demo --output-dir build/host_bridge_0_9/app
python tools/build.py --output-dir build/host_bridge_0_9/boot
```

第一条生成链接于0x08008000的Python业务固件0.9；第二条生成主控常驻引导0.4回归镜像，只链接USB/显示/电源基础部分。现有主控引导0.3即可安装0.9应用，升级外接设备不要求另刷主控引导。本轮没有执行任何烧录。

## 三类PY32依赖与构建

UL、IR、MOTOR及sensor_boot的第一方源码/构建脚本已进入本分支。Puya SDK按 `sensor_boot/dependencies.json` 固定为 `394627d99085c5eae72142503b0405b84428bb20`，从公开仓库恢复，不复制其它工作树的虚拟环境或把SDK目录误提交为裸gitlink：

```powershell
git clone https://github.com/OpenPuya/PY32F002B_Firmware.git UL/firmware/vendor/PY32F002B_Firmware
git -C UL/firmware/vendor/PY32F002B_Firmware checkout 394627d99085c5eae72142503b0405b84428bb20
git -C UL/firmware/vendor/PY32F002B_Firmware status --short
```

SDK也可放在已有独立路径，以 `-SdkRoot` 或 `KLQ_PY32_SDK` 指定。新SDK路径受本项目忽略规则保护，版本和来源以依赖文件为准。SDK的CMSIS/LL驱动、启动文件及原许可证均由原库提供，本项目不替换其许可。本轮复用共享工作区干净的固定SDK作只读构建：

```powershell
& sensor_boot/tools/build.ps1 -SdkRoot F:/work/KLQ_V2/Codex/UL/firmware/vendor/PY32F002B_Firmware -Python D:/Python/Python312/python.exe
```

另一台电脑应改成本机路径，也可恢复到默认目录后直接执行 `sensor_boot/tools/build.ps1`。该入口按ul/ir/motor重建6个镜像及各自工厂HEX、升级容器和manifest。直接运行旧独立UL/IR构建入口仍会生成0地址应用，不能替代统一升级包。

## 软件回归与打包

```powershell
& sensor_boot/tests/run_tests.ps1
& sensor_boot/tests/run_host_bridge.ps1
& sensor_boot/tests/usb_host/run_tests.ps1
& sensor_boot/tests/run_business_tests.ps1
& sensor_boot/tests/run_pipeline_tests.ps1
python -m unittest discover -s sensor_boot/tests -p 'test_*.py' -v
python tools/test_blocks_sim.py --report build/host_bridge_0_9/block_sim.json
python -B tools/test_block_tool_cleanup.py
& IR/tests/run_host_tests.ps1
python tools/package_host_release.py
```

先生成三类镜像和native模拟器，再运行Python链路测试，确保无跳过。完整链路保持在模拟引导到END提交，不执行ARM应用跳转；不接真实串口、USB硬件、探针或Flash。包位于 `build/host_bridge_0_9/release/`，build仍不入Git，跨电脑可重建或单独转交ZIP并核对其外部JSON/SHA-256。

实际安装前须另行确认主控COM、目标板型/UID、供电、首次工厂引导及安全输出；本轮授权不包含自动烧录。具体使用见 `sensor_boot/docs/usb_bridge_v1.md`，业务合流证据见 `verification_20261029_host_upgrade.md`。

## IR实板升级修复后的当前基线

同日后续已经用户明确授权完成IR仅引导1.0.2安装，再从主控USB实测两次应用完整升级。当前PC工具在BEGIN前拒绝引导低于1.0.2，不能继续使用早期0.9合集中的factory/PC工具。主控本身仍为0.9。本节覆盖前文合流时“未烧录”的历史边界，具体范围见verification_20261029_ir_via_host.md。

```powershell
& sensor_boot/tools/build.ps1 -Device ir -OutputDir build/ir_boot102_20261029 -SdkRoot F:/work/KLQ_V2/Codex/UL/firmware/vendor/PY32F002B_Firmware
& sensor_boot/tests/run_flash_hw.ps1
& sensor_boot/tests/run_tests.ps1
python -m unittest discover -s sensor_boot/tests -p 'test_*.py' -v
```

Flash模型运行实际驱动C，以进程虚拟内存和寄存器代理替代MMIO，只替换未执行的ARM跳转汇编。旧1.0.1源码快照随tests/flash_host入库，不依赖另一分支提交。此模型复现EOPIE契约，仍不替代硅片Flash/中断时序验收。

仅引导安装需特别注意DFP1.2.6声明4096 B擦除扇区，直接给4516 B BIN做sector erase会碰到0x1800应用区。本轮经用户授权使用128 B物理页RAM写入器，并整片比较证明APP/metadata/选项未变；不要在仅引导授权下使用“擦后补回APP”。当前受限RAM helper源码/构建入口为sensor_boot/tools/boot_repair_ram.c、build_ram_repair.ps1；只供明确目标、备份和范围校验后的恢复使用，不能作为普通USB升级流程。


0.13当前入口：python tools/test_python_motor_faults.py（188断言含负对照）、python tools/test_motor_snapshot.py（8组）、python tools/test_python_motor_live_tool.py（14组fake），实物用test_python_motor_live.py。故障/快照测试不接硬件；live会主动覆盖并清理RAM，用户人工观察优先examples/motor_blocks_demo.py正常下载保留RAM。0.13实际镜像在build/python_motor_20261030/app，验证见verification_20261030_python_motor.md。
