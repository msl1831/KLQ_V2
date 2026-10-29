# UL 统一引导集成核对（2026-10-29）

## 当前结论

UL 已适配 `sensor_boot` 共用引导，可生成首次 SWD 工厂镜像及后续经 KLQ 主控 USB/UART 更新的应用包。此前本对话中“引导未实现”的结论已被同日共享工作区实现替代。当前通过编译、静态镜像和原生模拟验证，尚未烧录或实板验收。

本轮先读公共交接第 14/15 节、环境第 8/9 节并核对 `main@63d1ea38f69fbd478f92751855f238340b6fa853`。保留主控、IR、电机、语音等已有未提交改动；仅修正 UL 服务一处复位等待检查、增加 UL 回归测试和本文。未修改共用引导/主控源码或公共交接，未切分支、提交或推送。

## 布局、身份与启动

| 核对项 | 已核实的实现 |
| --- | --- |
| 引导 | `0x08000000..0x080017FF`，6144 B；替代旧 UL 末尾 Load Flash 方案 |
| 应用 | `0x08001800..0x08005F7F`，18304 B |
| 有效记录 | `0x08005F80..0x08005FFF` 独占 128 B |
| 向量 | 应用前 192 B，map 中 `__Vectors=0x08001800`、`__Vectors_End=0x080018C0` |
| 固定身份 | `0x080018C0` 的 16 B 为 `KID1, board=1, hw=1, base=0x08001800, ABI=1` |
| 代码 | 从 `0x080018D0` 起，使用 `sensor_boot/linker/app.sct` |
| 应用 INFO | class=1、board=1、hw=1、sensor_type=1、version=`0x00010001`、capabilities=15、max_payload=132、max_image=18304 |
| UART / SWD | PA3 TX、PA4 RX、115200 8N1；PA2/PB6 保留 SWD |

实际应用 BIN 初始 SP=`0x200007E0`、复位入口=`0x08001975`，栈对齐、SRAM 范围和 Thumb 入口范围均有效。offset `0xC0` 实际字节：`4B 49 44 31 01 00 01 00 00 18 00 08 01 00 00 00`。

`UL/tools/build_sensor.ps1 -Bootloader` 加入 `KLQ_BOOT_APP`、`SENSOR_UL`、`SWD_DELAY`，链接固定身份源 `sensor_boot/src/app_identity.c` 和共用 scatter。厂商 `SystemInit` 会将 VTOR 设回 Flash 起点，因此 `main` 的首个动作是 `sensor_app_enter()`：关中断、设置 VTOR、DSB/ISB、再开中断，然后进行板级/串口/测距初始化。引导跳转前已有 SysTick/NVIC/UART 清理。

## 升级入口与安全输出

可升级构建接受空 payload 的 `ENTER_BOOTLOADER(0x01F0)`。只有 OK 响应确实进入 TX 队列才设置 `reset_pending`，随后同时满足 TX 队列为空、USART TC 完成及至少经过 2 ms 后软件复位。正常上电/软件复位开放 500 ms 引导窗口，主控自动发送 HOLD；无有效记录或 CRC 错误时保持引导。没有 RAM 令牌或独立看门狗救援，极端失联仍可能需要人工拔插电源端子。

本轮发现并修正的缺口：原 `device_service_handle_frame` 先检查请求长度、后检查 `reset_pending`；等待 ACK 发完时，错误长度请求仍可追加 BAD_LENGTH 响应。现将 pending 检查移到最前，阻止后续正常/畸形请求追加 TX 数据。正常请求布局、测距及版本不变。

共用引导的 UL GPIO 初始化先将 PA5 TRIG、PA6 LED 清低，再设置输出；应用继续使用 BSRR/BRR 原子写，保留 PA6 既定慢闪/异常快闪。应用期间测距和工作灯继续按主循环工作，复位后由引导保持 PA5/PA6 低电平。上述是源码检查；实际电平、脉冲和启动时序尚未板测。

## 正确的交付入口

在仓库根目录执行：

```powershell
& sensor_boot/tools/build.ps1 -Device ul
```

该入口统一构建引导、调用 UL 的 `-Bootloader` 应用模式并打包：

- 首次安装：`sensor_boot/build/ul/release/ul_factory.hex`，含引导、应用和有效记录。
- 后续升级：`sensor_boot/build/ul/release/ul_update.klq`；不能发送整个工厂 BIN。
- 配套信息：同目录 `ul_manifest.json`、`ul_release.zip`。

普通 `UL/tools/build_sensor.ps1` 默认仍从 `0x08000000` 链接独立应用，不具备常驻引导和在线更新能力。仅给该命令加 `-Bootloader` 只生成配套应用，不会生成完整首次安装文件。

首次工厂安装和主控相应固件实板验证通过后，后续通过主控升级的示例为：

```powershell
& .venv/Scripts/python.exe sensor_boot/tools/update.py --via-host --port COM10 --sensor-port 1 --image sensor_boot/build/ul/release/ul_update.klq
```

COM10/端口 1 为示例，按实际连接填写。主控应用须在 INFO 中声明 `EXT_UPGRADE=1`；当前转发实现标记为 0.3，基于 main 工作区，未合并 python-runtime 分支。命令及协议以 [统一引导](../../sensor_boot/README.md)、[UART 协议](../../sensor_boot/docs/protocol_v1.md)、[USB 转发协议](../../sensor_boot/docs/usb_bridge_v1.md) 为准。

## 本轮验证证据

工具：ARMCC 5.06 update 6 build 750；固定 Puya SDK `394627d99085c5eae72142503b0405b84428bb20`，SDK 工作区干净。

| 镜像 | ROM / 上限 | RAM / 上限 | 备注 |
| --- | ---: | ---: | --- |
| UL 共用引导 | 4464 / 6144 B | 1600 / 3072 B | RO 4444 B |
| UL 可升级应用 | 5776 / 18304 B | 2016 / 3072 B | RO 5716 B，含固定 1024 B 栈、无堆 |
| UL 独立应用回归 | 5756 / 20352 B | 2016 / 3072 B | 输出 `UL/tests/build/standalone` |

修复前后可升级应用 ROM/RAM 数值不变，仅代码排列及 CRC/哈希变化。当前生成包：

| 文件/字段 | 值 |
| --- | --- |
| 引导 SHA-256 | `86c032e11e9358570d1f66ad5ff34aee2375a6a81be8b79cc824bed388909f30` |
| 应用 SHA-256 | `628eab428ed4cf67c77a00eed9375e3c60967f464ca6155f291d26c762d5a4b0` |
| 应用 CRC32 | `0xCF112888` |
| 工厂 HEX SHA-256 | `d8c496c6b4c203d061c60cce4cca53fc7d669c9270fcbf7539b98bd038892281` |
| 升级 KLQ SHA-256 | `0cf7cedb4d8dc572c2b81f51f7ba64469b8d6223f9ec93e1172a85e48e8fc1d3` |
| Release ZIP SHA-256 | `3a8107024490fc5a95d8cc9f7f4eb2317cac9680b012e18717d5385dddf4c220` |

可重复的软件检查：

```powershell
& sensor_boot/tests/run_tests.ps1
& UL/tests/run_boot_entry_tests.ps1
& .venv/Scripts/python.exe -m unittest discover -s sensor_boot/tests -p test_tools.py -v
& UL/tools/build_sensor.ps1 -Configuration Release -OutputDir UL/tests/build/standalone
```

结果：共用实际 C 升级内核/协议三板型测试、UL/IR 进入引导/TX 排空/计时回绕通过；UL 新增 pending 畸形请求回归在修复前明确失败，修复后通过；Python 13/13 通过，包含工厂 HEX/BIN/manifest/ZIP、一致性/身份拒绝/丢 ACK 重传/同上电续传及给实际 C 引导实例发送生成应用。独立应用编译通过。

本轮没有重写共用引导或主控转发，没有连接任何 COM、DAPLink、GPIO 或 Flash。真实启动/VTOR、UART 进入与 HOLD 时序、测距/LED、掉电恢复、升级后默认运行仍需验收。公共 `sensor_boot/docs/build_evidence_20261029.json` 和旧主控 ZIP 记录的是本轮小修复前应用；当前 UL 包以本记录和新生成的 `ul_manifest.json` 为准，后续交付主控合集时需重新收集最新 UL 包。所有源码与文档仍为本机未提交内容。

## 同日首次烧录尝试：USB 枚举阻断

用户随后确认 UL 全新板已通过 DAPLink 连接并要求快速烧录，授权安装当前完整工厂 HEX。已再次核对工厂 HEX/BIN/manifest/KID1/向量与 CRC，身份为 board=1、hw=1，文件与上表一致。

普通环境及提升权限环境的 pyOCD 均报告 `No available debug probes are connected`。Windows 当前无 DAPLink/CMSIS-DAP，仅发现异常设备 `USB\VID_0000&PID_0002\6&1C446172&1&3`，状态为“未知 USB 设备（设备描述符请求失败）”；不能断言该未知设备一定是 DAPLink。没有读到目标 CPUID/UID，未连接 SWD、未擦除或烧录，也未修改驱动/复位其它设备。待重新插拔探针 USB、检查数据线/USB 口后，再识别具体探针及 PY32 目标，烧录并读回校验。

## 同日重试：UL 新板首次烧录通过

用户重新连接后要求重试。已识别唯一探针 `jixin.pro CMSIS-DAP_LU`、UID `LU_2022_8888`，按 Puya DFP 1.2.6 的 `py32f002bx5` 目标在 1 MHz SWD/halt 模式连接。CPUID=`0x410CC601`，MCU UID=`4150354C-3832360B-0064B862`，初始 Flash 前 16 B 均为 FF。空程序尝试运行时进入 lockup，后续完整烧录后正常启动。

烧写当前 `sensor_boot/build/ul/release/ul_factory.hex` 成功：擦除 16384 B（4 sectors），编程 10496 B（82 pages，含页补齐）。使用独立 pyOCD 会话重新核对同一 MCU UID，逐字节读回引导 4464 B、应用 5776 B 和末页 128 B 均一致；整片 24576 B 也与工厂 BIN 完全一致，SHA-256=`6aedbf653d0cda1de899aa682b319623b2126155e0f79ea9f3a6878bb924e1a0`，应用 CRC=`0xCF112888`。

复位等待后暂停确认：VTOR=`0x08001800`、PC=`0x080025C0`，位于应用；tick_ms=783、measurement_count=21，最近样本 `(seq=21, time_ms=742, echo_ticks=188, distance_mm=64, status=VALID)`。检查后已恢复运行。说明本板引导默认跳转、应用中断/节拍和实际回波采集首次验证通过；主控转发升级、UART 升级断电恢复和 LED 肉眼灯效仍未在本轮验证。未烧录 KLQ 主控或电机/IR 板，未修改选项字节。本节覆盖前一节“USB 枚举阻断”的当前状态。
