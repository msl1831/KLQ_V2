# 主控0.9开发与外接升级构建环境（2026-10-29）

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
