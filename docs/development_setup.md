# 开发环境、版本与新电脑安装

**2026-10-29 同步更新：** main已将现有UL/IR/MOTOR/共用引导、语音SDK输入及环境工具纳入版本管理，Puya SDK已正式登记为子模块。初次取main使用git clone --recurse-submodules，已有仓库拉取后执行git submodule update --init --recursive。下文9月24日关于“未入库/未登记子模块”的文字是历史审计；当前分支与功能边界以handoff_current.md顶部为准。最新主控0.9仍在codex/python-runtime@8db761a，该分支用sensor_boot/dependencies.json固定SDK，按它自己的环境文档恢复依赖。

VOICE/CI13XX_SDK_ASR_ALG_V2.7.12已包含定制源码、库、原始模型、WAV和命令表，克隆main不再需要通过SDK ZIP重新覆盖这些文件。VOICE/toolchains为本机安装目录，仍需准备所记录的厂商RISC-V GCC9.2.0工具链；Keil、Python和器件包同样按下表安装。原始工具链RAR位置和SHA-256在环境基准JSON中，尚未创建共享安装包下载地址。

基准采集：2026-09-24，公司 Windows 10 22H2（10.0.19045）。准确版本和第三方包校验值保存在 [environment_baseline.json](../tools/environment_baseline.json)，Python 全部 27 个已安装依赖保存在 [requirements.lock.txt](../tools/requirements.lock.txt)。

## 1. 需要保持一致的内容

| 软件/依赖 | 公司实际版本 | 同步要求 |
| --- | --- | --- |
| Keil MDK / uVision | MDK Plus 5.38；UV4 5.38.0.0 | 新电脑优先安装同版；命令行构建实际依赖下面的 ARMCC 工具 |
| ARM Compiler 5 | 5.06 update 6，build 750 | 严格固定，含 armcc/armasm/armlink/fromelf 和运行库。现有参数使用 ARMCC5/Microlib，ARMClang6 或 GCC 不能直接替代 |
| Geehy SDK | 上游 236fbc3ed9ba4e7a8f2e617dc783fd2bd1b746e1 的裁剪副本 | 使用 Git 已跟踪副本，保留 usbd_interrupt.c 的 USB suspend 修复；不要用新下载原版覆盖 |
| Geehy 器件包 | APM32E1xx_DFP 1.0.3 | 主控 DAPLink/SWD 烧录时保持一致；主控编译不依赖该包 |
| Python | 3.12.14，64 位 | 优先相同版本；其他 3.12 补丁版需要重新验证。不要直接升级到另一 minor 后假定兼容 |
| pyOCD / pyserial | 0.45.1 / 3.5 | 固定版本；传递依赖按 lock 文件安装 |
| Puya SDK | 394627d99085c5eae72142503b0405b84428bb20 | UL 必需，固定提交；当前公司副本干净 |
| Puya 器件包 | PY32F0xx_DFP 1.2.6 | UL SWD 烧录必需，官方 SDK 中已有 .pack |
| CI13XX SDK | ASR_ALG V2.7.12 + KLQ 定制 | 语音开发必需；不能只安装原版而遗漏 KLQ 的配置/命令表修改 |
| RISC-V GCC | 厂商 gcc_fix_raissrc 包中的 riscv-nuclei-elf-gcc 9.2.0 | 语音开发固定这个工具链包；只看同为 GCC 9.2.0 不足以确认库和补丁相同 |
| SDK GNU Make / Lua | 4.2.1 / 5.4.3 | 语音构建使用 SDK 自带版本及 busybox 等工具 |
| ci-tool-kit | 1.25 | 语音分区/整包工具固定版本和哈希 |
| PACK_UPDATE_TOOL | 4.0.5.000 | 使用厂商串口升级 GUI 时保持一致 |
| Git for Windows | 2.39.1.windows.1 | 不要求同一补丁版；必须能认证同一仓库并使用约定分支 |
| PowerShell | 7.6.5 | 本轮检查/构建使用该版；建议一致，Windows 自带 5.1 未在本轮重新验证 |
| Codex / 编辑器 | 不参与编译器输出 | 开始任务时使用仓库内交接文件；构建基线以工具版本、源码提交和依赖校验值为准 |

公司 Python 来自本机缓存的运行时，.venv/pyvenv.cfg 中含公司绝对路径。家中应安装/准备相同 Python，再创建新 venv；项目不要求同样的 Windows 用户名或 F: 盘。主控和 UL 可以配置编译器位置；语音 Makefile 的路径解析依赖空格分词，建议统一使用 C:/work/KLQ_V2 一类不含空格的短路径。

串口号、探针 UID、USB 设备实例和本机集线器 ID 都是机器/设备相关值，不是版本要求。不要照搬公司 COM123 或探针 UID。

## 2. 新电脑的主控开发步骤

先解决 [交接文档](handoff_current.md) 中的分支与未提交文件问题。本节的锁文件和检查工具是本次新增材料，必须先被提交到家中实际检出的共享分支才能执行。

当前最新已推送主控功能位于 codex/python-runtime。初次克隆可以显式选择该分支：

~~~powershell
git clone --branch codex/python-runtime https://github.com/msl1831/KLQ_V2.git C:/work/KLQ_V2
cd C:/work/KLQ_V2
git status --short
git log -1 --oneline
~~~

审计时对应 89bd9ac；后续以实际共同约定的分支最新提交为准。GitHub 访问与 Git push 需要这台电脑自己的仓库认证配置。

安装 MDK 5.38 和 ARM Compiler 5.06 update 6 build 750，并确保编译器运行库完整。先确认工具版本：

~~~powershell
$env:KLQ_ARMCC = 'C:/Keil_v5/ARM/ARMCC/bin'
& "$env:KLQ_ARMCC/armcc.exe" --vsn
& "$env:KLQ_ARMCC/armlink.exe" --vsn
~~~

KLQ_ARMCC 是当前 PowerShell 会话变量；换终端时重新设置，或由本机环境设置保存。默认位置正是 C:/Keil_v5/ARM/ARMCC/bin。

以实际 Python 3.12 安装路径创建环境，以下 C:/Python312 仅为路径示例：

~~~powershell
& C:/Python312/python.exe --version
& C:/Python312/python.exe -m venv .venv
& .venv/Scripts/python.exe -m pip install -r tools/requirements.lock.txt
& .venv/Scripts/python.exe -m pip check
& .venv/Scripts/python.exe tools/check_environment.py --require-runtime
& .venv/Scripts/python.exe tools/build.py
& .venv/Scripts/python.exe tools/build.py --demo
~~~

不需要建立新的 .uvprojx 来构建主控；tools/build.py 调用编译器并生成 scatter 文件。项目已有的 Keil 工程、.sct、.ini、.FLM、库文件仍要保留。

检查工具只读取本地文件、软件版本、包信息和 Git 状态，不连接探针或操作硬件；退出码 1 表示缺失/不匹配，加入 --strict 后警告也会使其失败。默认检查不能证明已经 push，仍需执行交接流程的远端提交核对。

首次构建后核对 map 中 Flash/RAM 用量。89bd9ac 系统固件参考值是 20412 字节，SHA-256 为 ddbcb3c2128d60a477cb01c2a09c4373f7db448e293662d7ce1b2552ed7795f3。不同提交不要拿该哈希作为通过条件。

## 3. 主控烧录环境

仅源码开发和编译无需接硬件。进行 SWD 下载时另安装 Geehy.APM32E1xx_DFP.1.0.3.pack：

- 公司已下载原包位于 %LOCALAPPDATA%/Arm/Packs/.Download/Geehy.APM32E1xx_DFP.1.0.3.pack。
- 公司已安装目录为 %LOCALAPPDATA%/Arm/Packs/Geehy/APM32E1xx_DFP/1.0.3。
- 原包 SHA-256：0ec35b01f62f75f8012493c1e6e5ae02e6da4b88235afa8b7af1aeaa35d9559b。
- 原包不在 Git 中，需提前保存到家中可访问的位置，或安装相同厂商版本。
- tools/dap_flash.py 可以用 --pack 显式指定包或安装目录，--probe 指定实际探针。

先做环境检查与设备枚举：

~~~powershell
& .venv/Scripts/python.exe tools/check_environment.py --require-runtime --flash
& .venv/Scripts/python.exe -m pyocd list
& .venv/Scripts/python.exe tools/klq_usb.py ports
~~~

枚举到 DAPLink 不代表已经确认连着哪个 MCU；按板卡和接线确认主控 APM32E103RE 或 UL PY32F002Bx5 后再执行各自烧录步骤。当前脚本默认探针 UID 是公司设备值，家中必须显式核对。

USB 已有有效引导的板子通过 klq_usb.py 更新系统固件；首次烧录/恢复则按 README_BOOTLOADER.md 操作。status_dashboard.py 和其他串口工具不可同时占用同一 CDC 口。

tools/klq_hub_power.ps1 包含公司特定集线器标识，tools/refresh_klq_usb.ps1 是排障脚本；两者都不属于新电脑的环境安装步骤。

## 4. UL 超声波工程

UL 自研文件必须先进入共同的 Git 分支。厂商 SDK 当前是独立嵌套仓库，正式入库前需选择规范子模块或源码导入策略，避免父仓库只有 gitlink 而没有 .gitmodules。

若采用外部固定 SDK 恢复方式，且目标目录尚不存在，可用以下命令还原本机同一提交；不要覆盖已有修改的目录：

~~~powershell
git clone --no-checkout https://github.com/OpenPuya/PY32F002B_Firmware.git UL/firmware/vendor/PY32F002B_Firmware
git -C UL/firmware/vendor/PY32F002B_Firmware checkout --detach 394627d99085c5eae72142503b0405b84428bb20
git -C UL/firmware/vendor/PY32F002B_Firmware status --short
~~~

UL 与主控可以使用同一 ARMCC5。UL 构建脚本本轮增加 -ArmccBin 参数，并支持 KLQ_ARMCC 环境变量：

~~~powershell
& .venv/Scripts/python.exe tools/check_environment.py --ul
& UL/tools/build_sensor.ps1 -Configuration Debug -ArmccBin $env:KLQ_ARMCC
~~~

默认 Debug 使用 -O1 -g，Release 使用 -O2；两台比较构建结果时必须选择相同配置。

现有 UL/tools/flash_sensor.ps1 固定从 UL/.venv 寻找 pyOCD，因此需要单独建立该环境：

~~~powershell
& .venv/Scripts/python.exe -m venv UL/.venv
& UL/.venv/Scripts/python.exe -m pip install -r tools/requirements.lock.txt
& UL/.venv/Scripts/python.exe -m pip check
~~~

烧录使用 SDK 中 Packs/MDK/Puya.PY32F0xx_DFP.1.2.6.pack，目标 PY32F002Bx5；与主控的 Geehy 包不能互换。Pack 哈希由环境检查脚本核对。实际下载前显式核实 flash_sensor.ps1 的 -ProbeUid 和镜像路径。

UL/tools/inspect_io.mjs 等一次性表格分析工具的 Node 依赖不在固件构建路径中，不作为主控/UL 日常编译的安装前置条件。

## 5. CI1303 语音环境与缺口

语音目前没有可直接从 Git 恢复的完整工作提交，也没有本次新验证的全自动打包脚本。已有临时包可作为历史试验制品保存，不能用它代替源码、模型、命令表和工具链的迁移。

先准备 environment_baseline.json 中校验值相同的 SDK ZIP 和 GCC RAR，分别恢复至：

- VOICE/CI13XX_SDK_ASR_ALG_V2.7.12
- VOICE/toolchains/gcc_fix_raissrc

再应用交接文档中列出的 4 个 KLQ 定制文件及 SDK .gitignore 修改。模型 .dat/.fefixbin458、预编译 .a、源 WAV、命令 .xlsx 都要保留。SDK 自带 make/lua/busybox/lame/ci-tool-kit 也应按同一包恢复。环境检查：

~~~powershell
& .venv/Scripts/python.exe tools/check_environment.py --voice
~~~

代码构建路径（以下 make 会生成代码分区，不等于整包已验证）：

~~~powershell
$repoRoot = (Get-Location).Path
$voiceSdk = Join-Path $repoRoot 'VOICE/CI13XX_SDK_ASR_ALG_V2.7.12'
$voiceGcc = Join-Path $repoRoot 'VOICE/toolchains/gcc_fix_raissrc/bin'
$previousPath = $env:PATH
try {
    $env:PATH = "$voiceSdk/tools/build-tools/bin;$voiceGcc;$previousPath"
    Push-Location "$voiceSdk/projects/offline_asr_alg_pro_sample/project_file"
    try {
        & "$voiceSdk/tools/build-tools/bin/make.exe" -j4
        if ($LASTEXITCODE -ne 0) { throw 'CI1303 build failed' }
    } finally { Pop-Location }
} finally { $env:PATH = $previousPath }
~~~

必须补齐并验证的打包细节：

- 样例 makefile 使用 libbnpu_core_alg_pro_null.a 合并 user_code；公共 common_firmware.mk 中旧 user_code 目标却引用当前不存在的 libfbin.a，不能直接把公共 make firmware 视为可用的完整流程。
- utils/firmware_pack.sh 把 --chip-name 写死为 CI130X；历史临时包使用显式 ci-tool-kit mf -f v2 --chip-name CI1303 和 4 MiB ROM 参数生成。
- cmd-info 输入要指定实际 .xlsx 文件，不能只传目录。模型、语音、用户代码和命令文件的分区尺寸需按实际生成结果计算。
- config.ini 包含旧的 G:/CI/... 烧录镜像路径，换电脑后要在工具中重新选择实际文件；其中打包参数需要保留审阅。本次只解除其 Git 忽略，没有重写这些参数。
- “小乐小乐”定制模型尚缺；已有默认模型带 CI1306 文件名，只能按现有说明用于 CI1303 样机联调。
- 语音运行接口是 CI1303 UART1（PA2/PA3）连接 KLQ UART4；编程入口是 UART0 和 PA4/PG_EN。使用 PACK_UPDATE_TOOL 时按这两个用途区分接线。

## 6. 验收新电脑是否准备好

1. Git 当前分支及提交与离开上一台电脑时记录一致，所需 UL/语音源码和文档已实际拉取到。
2. 按开发范围运行 check_environment.py；所有失败项解决，警告逐项说明。
3. 两个主控镜像从新目录编译通过，核对 map 和提交对应的镜像哈希；UL 另编译自己的目标。
4. 若继续语音，完成代码、所有分区及整包的干净重建，补写确切命令和校验值。
5. 必要时再做板上验证；编译成功与端口出现不替代硬件验收。
6. 在家中做一次小的文档提交和 push，回公司 fetch 后核对该提交可见，形成第一次往返同步证据。

工具版本升级时，作为单独变更更新本文件、lock/基准文件和验证记录；不要只在其中一台电脑静默更新编译器或 SDK。

## 7. IR 双路红外工程（2026-10-26）

IR 使用与 UL 相同的 PY32F002BF15U6TR、ARMCC5.06u6 build750 和固定 Puya SDK `394627d99085c5eae72142503b0405b84428bb20`，固件源码、IO 和类型身份独立。默认 SDK 路径为 `UL/firmware/vendor/PY32F002B_Firmware`，也可用 `IR/tools/build_sensor.ps1 -VendorRoot <SDK目录>` 或 `KLQ_PY32_SDK` 指定；未复制SDK，其既有嵌套仓库交接问题仍需按第4节处理。

```powershell
& IR/tools/build_sensor.ps1 -Configuration Release
& IR/tests/run_host_tests.ps1
& .venv/Scripts/python.exe -m unittest discover -s IR/tests -p test_uart_tool.py -v
```

Native测试默认使用本机VC98 `CL.EXE`，可用`-Compiler`指定兼容环境；该工具只用于主机模拟验证，不参与目标固件构建。测试输出在`IR/tests/build`，固件输出在`IR/firmware/app/build`，均可重新生成。IR镜像从`0x08000000`启动，没有独立引导/串口升级器；SWD目标为PY32F002Bx5，严禁套主控或电机配置。实板连接与烧录需单独按实际探针/目标核实。

主控本轮接入使用`tools/build.py --demo --output-dir build/ir_host_after`重建；该main工作区包含原有未提交语音改动，尚未合入`codex/python-runtime`。具体资源、哈希和测试边界见`IR/docs/verification_20261026.md`。

## 8. UL / IR / MOTOR 共用引导（2026-10-29）

统一入口为 [sensor_boot/README.md](../sensor_boot/README.md)，协议为 [protocol_v1.md](../sensor_boot/docs/protocol_v1.md)。依赖仍为 ARMCC5.06u6 build750、Python3.12.14 和固定 Puya SDK；没有新增固件运行库或堆。SDK 路径可由 `-SdkRoot` / `KLQ_PY32_SDK` 指定，编译器由 `-ArmccBin` / `KLQ_ARMCC` 指定，Python 可由 `-Python` 指定。

```powershell
& sensor_boot/tools/build.ps1
& sensor_boot/tests/run_tests.ps1
& .venv/Scripts/python.exe -m unittest discover -s sensor_boot/tests -p test_tools.py -v
```

输出分别为 `sensor_boot/build/ul`、`ir`、`motor`，其 release 中有工厂 HEX/BIN、板型升级容器、SHA-256 manifest 和 ZIP。首次 SWD 安装使用对应工厂 HEX，后续 UART 使用 `*_update.klq`。三种应用同从 0x08001800 链接，末尾独占128 B有效记录；UL/IR原默认独立构建及电机历史构建继续保留。各脚本新增 `-OutputDir`；UL/IR 的 `-Bootloader` 和 MOTOR/build_motor.ps1 的 `-Bootloader` 选择共用引导配套应用，关闭厂商 SWD 延时并加入固定应用身份。

PC升级工具依赖已有pyserial3.5，保留直连UART用法；经主控升级增加 `--via-host --sensor-port 1`，`--port`填写主控USB COM。主控应用0.3已接入转发。统一引导初始证据见 [verification_20261029.md](../sensor_boot/docs/verification_20261029.md)，主控转发证据见 [USB验证记录](../sensor_boot/docs/verification_20261029_usb_bridge.md)，均未连接探针、烧录或板测。

## 9. 主控 USB 外接端口升级转发（2026-10-29）

```powershell
& .venv/Scripts/python.exe tools/build.py --demo --output-dir build/usb_sensor_bridge/app
& .venv/Scripts/python.exe tools/build.py --output-dir build/usb_sensor_bridge/boot
& sensor_boot/tests/run_host_bridge.ps1
& sensor_boot/tests/usb_host/run_tests.ps1
& sensor_boot/tests/run_pipeline_tests.ps1
& .venv/Scripts/python.exe -m unittest discover -s sensor_boot/tests -p 'test_*.py' -v
```

主控应用基于当前main工作区，版本标记0.3，未合并codex/python-runtime。新桥接位于现有external_ports.c中，主控引导仍通过KLQ_DEMO_APP排除UART模块；无新增构建依赖。Native测试沿用VC98。端口独占停止当前用户程序并暂停目标端口普通轮询，退出不自动恢复旧动作。

可转交包为 `build/usb_sensor_bridge/release/klq_usb_sensor_bridge_0.3.zip`，包含主控应用BIN/HEX、电脑工具、pyserial依赖清单、三类升级容器和manifest。对应 [USB说明](../sensor_boot/docs/usb_bridge_v1.md)；外接板首次安装仍使用sensor_boot各板型工厂HEX。主控、外接板的实际安装及验收需另行执行，编译和原生模拟不证明实板通过。
