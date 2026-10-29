# KLQ 接力开发与当前进度

## 最新 Git 同步状态（2026-10-29）

用户授权“同步最新状态到GIT”。本次将公司工作目录中现有主控桥接、UL/IR/MOTOR、共用引导、语音SDK及其模型/库/命令表、环境工具与交接记录收录到 **main**；本节之后的按日期记录保留为历史，不再代表全部当前状态。

- **main：本文件所在的同步提交。** 包含此前未提交的外接板、共用引导和语音试验输入。其主控应用仍是0.3桥接演示基线，未进行Python功能合并；不能用它覆盖已运行的主控0.9而期望保留全部功能。
- **codex/python-runtime：8db761abff784cc3dd023a378b03def718a138c6，已核对远端一致。** 最新主控0.9的合流源码为ce714f7，包含Python业务和外接升级桥接。0.9应用24176 B，SHA-256 d255a4135526c10e24b18b01dd2dca1cbba46a5168b601bc348944355085a4b8；当天通过主控USB安装、END CRC、RUN、INFO和ECHO。证据在该分支docs/verification_20261029_usb_install.md。
- **UL/IR：** UL已有首次烧录/读回和应用启动证据，共用引导1.0.1尚未安装到UL。IR已重新安装共用引导1.0.1，新增读回10564 B一致、默认启动及双路采样38→48的证据，见[IR新版引导烧录](../IR/docs/reflash_boot101_20261029.md)。真实主控转发升级、故障注入和断电恢复仍待验收。
- **PY32电机：** 同步期间已完成共用引导1.0.1和应用2.0.0首次烧录，整片24 KiB读回、复位跳转、SysTick推进、默认PWM=0及PA0/PA1低电平有证据，见[MOTOR首次烧录记录](../MOTOR/docs/flash_20261029.md)。实际转动、UART升级和250 ms失联停机仍待板测。
- **Puya SDK：** 已正式登记.gitmodules和gitlink，固定394627d99085c5eae72142503b0405b84428bb20；子仓库干净，上游为OpenPuya/PY32F002B_Firmware。普通父仓库归档不包含子模块内容，需递归初始化。
- **保留本机的内容：** build、Objects/Listings、中间文件、虚拟环境、tmp、outputs和VOICE/toolchains均不在本次源码提交内。语音SDK本身、必要预编译库、模型和源音频已纳入；安装软件及语音GCC仍按development_setup.md/environment_baseline.json另行准备。文件没有被删除。
- **共用引导最新修复：** 1.0.1已修复RUN发送/TC超时后仍跳转的问题，并堵住500 ms自动启动旁路；UL/IR/Motor软件回归和重建已完成，见[sensor_boot验证记录](../sensor_boot/docs/verification_20261029_run_tx.md)。本次同步已收录此修复；安装它需要新版工厂镜像，应用升级容器不能更新引导。该修复尚未合入codex/python-runtime；Motor和IR已安装1.0.1，UL尚未安装新版引导。
- **UL新需求与诊断：** 用户已确认测距芯片改为RCWL9610A，GPIO接口不变。偶发0 mm已定位到无效NO_ECHO状态展示，并在未改动的驱动中复现时间戳/状态竞态；修复与触发周期对照尚未实施，见[诊断记录](../UL/docs/rcwl9610_zero_diagnosis_20261029.md)。当前资料已由用户替换为UL/RGB超声波测距芯片RCWL-9610A-V2.0.pdf，反馈电阻已确认753（75 kΩ）。已按A版资料完成诊断修订：50 ms周期、753反馈电阻及R9量程调节；固件修复尚未实施。旧V3.0手册的时序/量程/电阻结论作废；不能继续按旧CS100A的8 m范围作承诺。
- **已知待办：** 两条主控开发线仍并存，待后续按功能整合；主控分支8db761a增加了UL/IR实测及重启问题记录。语音工具链需另行安装。本次Git同步任务自身没有执行硬件访问。

本次从Git快照构建和回归的结果见[同步验证记录](git_sync_verification_20261029.md)：主控及UL/IR/Motor均通过，Python升级30项和IR工具10项通过，环境0失败、1条分支提示。尚未完成另一台电脑实装；新版共用引导的实板范围以各板最新烧录记录为准。

另一台电脑取得main的完整源码：

~~~powershell
git clone --recurse-submodules --branch main https://github.com/msl1831/KLQ_V2.git C:/work/KLQ_V2
# 已有同分支且工作区干净时：
git pull --ff-only
git submodule update --init --recursive
~~~

继续主控0.9开发时，应检出codex/python-runtime并阅读该分支交接；本次未合并或改写该分支。main收录的语音试验和环境材料也不能被误认为已纳入0.9。两端结束工作后都必须commit/push，并比较本地HEAD与对应远端分支，不用聊天或本地stash代替。

---

最新增量（2026-10-29）：UL/IR/MOTOR共用引导及主控USB→外接端口升级转发均已完成构建和软件验证，见第14/15节。当前仍是 `main@63d1ea3` 加未提交工作区，未合入python-runtime。前部2026-09-24审计及旧板记录保留；当前用法以第15节及sensor_boot文档为准，尚无本轮实板验证。

核对日期：2026-09-24。本文区分 Git 已发布内容、公司电脑工作区内容和历史实板记录。接力前先核对分支和提交，再读需求；登录同一账号不能替代代码提交、依赖安装和工作记录。

## 1. 当前可交接程度

**主控最新已提交版本可以从 Git 重建；完整项目目前还不能仅靠一次 clone 恢复。** UL、语音和本次补充的交接材料仍需要整理并提交到双方共同使用的分支。本次审计没有提交、推送、切换、合并分支或烧录硬件。

2026-09-24 实际查询 GitHub 远端分支，与本地提交一致：

| 分支 | 提交 | 内容及限制 |
| --- | --- | --- |
| main | 63d1ea38f69fbd478f92751855f238340b6fa853 | 主控旧基线；需求 V0.5；没有 Python 解释器及最新电源键功能 |
| codex/python-runtime | 89bd9acd63d3a8691616f566c2af4b3d25face76 | 比 main 多 7 个提交；Python 运行时、电源键、需求 V0.8 和最新验证记录 |

当前公司工作目录是 main。另一个工作树是 C:/Users/Administrator/.codex/worktrees/python-runtime/Codex，审计时该工作树干净。工作树的位置是本机实现细节，家中电脑可以在普通 clone 中直接检出同名分支。

当前 root/main 中的 bootloader/main.c、tools/build.py 修改和新增 audio_link.c/.h 属于尚未提交的 UART4 语音联调。最新 Python 分支从干净的 main 创建，没有带入这组修改。合流时 main.c 与 build.py 都需要实际合并和编译，不能整文件覆盖。

## 2. 需求和资料的读取顺序

1. 本文：识别分支、最新进度、待办和验证边界。
2. 根目录 KLQ功能需求文档_V0.5.md：文件名保留历史名称；codex/python-runtime 分支中的正文已经是 V0.8（2026-09-23），main 中正文仍是 V0.5。
3. docs/architecture_current.md 和 docs/decisions/001_sensor_access_strategy.md。
4. Python 分支的 docs/python_api_v0.1.md、docs/host_usb_protocol_v0.1.md：26 个非语音积木映射及 USB 协议。
5. README_BOOTLOADER.md：分区、构建、更新和恢复方法。
6. 对应日期的 verification 文档；旧交接文档中的“待测试”可能已经被较新的验证记录覆盖。
7. UL/超声波传感器固件架构_V0.1.md、UL/docs/klq_sensor_protocol.md、UL/firmware/README.md。
8. VOICE/KLQ_CI1303_TEST_NOTES.md：独立的临时语音试验边界。

原始 IO.xlsx、蓝牙芯片手册已跟踪。UL 的 IO 表和芯片手册在本机但未跟踪。Python API 文档引用的《科乐奇指令表》原始工作簿未在当前仓库文件清单中找到；已有接口映射可用于开发，原始资料仍应归档以便以后核对。

## 3. 已确定的设计约束

- 主控是 APM32E103RET6；UL 是 PY32F002BF15U6TR；语音是 CI1303。三个目标各自构建、选择烧录目标和验证。
- 内部 Flash：引导起点 0x08000000、32 KiB；系统固件起点 0x08008000、上限 0x77800；最后 2 KiB 为有效镜像记录。用户 Python 源码不写入这些区域。
- Python 是固定内存的受限子集，不是完整桌面 Python。当前一个 16 KiB RAM 程序，复位/断电清空；完整程序在仅 USB 断开重连时保留。新的 BEGIN 停止旧程序并覆盖同一缓冲。
- 程序完成、停止或错误后保持有效文本，可再次 RUN；不支持 try/except、变量、任意模块等，准确语法以 API 文档为准。
- 普通传感器 API 读取后台静态快照；离线、过期和不支持必须明确报告。运行时 SC7/CS100A 调度分别为 50/25 Hz，待机为 10 Hz；USB 日志频率和网页刷新率不是传感器采样率证明。
- PC4 用于系统短按运行/停止和 1.5 秒长按电源控制；PC3 是用户按键。待机为双眼动画。
- 停止、通信、电源响应优先；采用固定缓冲、静态数据、有界循环和无堆实现，修改固件后检查 linker map。
- 电机和红外设备协议尚缺，现有 API 明确报告不支持；安全停止接口存在不代表物理电机已停止。
- 语音仍是独立临时联调，不属于当前 26 个非语音 Python API。蓝牙下载、菜单、外部 Flash 保存等不能当作已完成能力。

## 4. 当前进度与下一步

| 子系统 | 当前证据 | 剩余工作 |
| --- | --- | --- |
| USB 引导/主控更新 | 协议、CRC、有效记录、中断恢复、RUN/RESET 有实板记录 | 最新重建引导与旧板上引导大小不同，需按新镜像单独安排硬件回归 |
| Python/RAM/显示 | 系统固件 0.7；循环、等待、错误、STOP、运行中替换和 USB 重连通过记录 | 上位机按接口生成代码；完整业务积木验收 |
| PC4/电源/双眼 | 短按和长按软件行为已记录验证 | 电池供电下真正冷启动/断电及 PC5 时序仍待测 |
| SC7/用户键 | 快照、倾斜条件、PC3 去抖和接口已有实现 | PC3 有效电平、实际安装方向及标定仍需实物确认 |
| KLQ 外接端口/CS100A | INFO/GET_SAMPLE、掉线重连有记录；独立给传感器供电后端口 2 恢复发现 | 核实电池和端口供电；不可把 DAPLink 给 MCU 供电当作传感器供电有效 |
| UL 传感器固件 | 本机有源码、协议、脚本和 Puya SDK；尚未入父仓库 | 入库并固定 SDK 获取方式；引导升级、看门狗等按 UL 文档边界推进 |
| 电机/红外 | 语法和 API 接口已固定 | 提供实际设备 INFO/控制协议和失联停机机制后接入 |
| CI1303 语音 | 本机有 SDK、修改的样例、命令表、提示音和临时包 | 固化可复现打包步骤；自定义唤醒模型及完整唤醒/ASR 实测未完成 |
| 蓝牙/菜单/低电量 | 需求存在，尚未形成完整验证闭环 | 协议、交互、电池规格及硬件条件明确后实施 |

最新实板记录位于 Python 分支：

- docs/verification_20260923_runtime_faults.md：系统固件 0.7，RAM 程序和解释器容错。
- docs/verification_20260923_power_on_off.md：引导 0.3、长按电源和通信回归，明确无电池/DAPLink 供电的限制。
- docs/verification_20260915_external_ports.md：包含 2026-09-23 的传感器供电排障补充。
- docs/verification_20260921_python_runtime.md：早期接入记录，不能覆盖后续更完整的证据。

本次只进行软件环境及编译检查，没有重新验证板上动作。语音默认包使用“智能管家”，不是“小乐小乐”量产唤醒模型；播放 ACK 不等于唤醒和识别已经通过。

## 5. Git 完整性审计与应保留的文件

| 内容 | 当前状态 | 交接处理 |
| --- | --- | --- |
| 主控源码、88 个 vendor 文件、构建/烧录/测试脚本、需求/IO/手册 | main 已跟踪；最新功能在 Python 分支 | 都保留；先统一开发分支 |
| Python 解释器、runtime、电源键、API/USB/验证文档 | 已推送 codex/python-runtime | 作为最新非语音主控基线，随后经过验证合入 main |
| audio_link.c/.h、main.c/build.py 的语音修改 | 公司目录未提交 | 完整保存在单独语音工作提交/分支，再决定如何接入最新主控 |
| UL 自研源码、linker/application.sct、脚本、协议/架构/IO/手册 | 全部未提交 | 必须入库；不是过程文件 |
| UL/firmware/vendor/PY32F002B_Firmware | 独立嵌套 Git 仓库；父仓库没有 .gitmodules，也没有已跟踪 gitlink | 固定 394627d99085c5eae72142503b0405b84428bb20；正式登记子模块或导入所需源码及声明。直接 git add 整个 UL 可能仅生成不可恢复的裸 gitlink |
| VOICE/CI13XX_SDK_ASR_ALG_V2.7.12 | 样例、源码、预编译库、模型、WAV、命令表均未提交 | 保存有修改的源文件和所有不可再生成输入；.a/.lib/模型不是编译缓存 |
| 语音 firmware/config.ini | 原 SDK 规则忽略 | 本次已加入精确例外；配置含旧 G: 烧录文件路径，家中需手动选择实际镜像，不能照搬 |
| VOICE/toolchains | GCC 文件约 500 MiB，另有 OpenOCD | 建议用固定版本安装包/共享制品和 SHA-256 恢复；目前没有已登记的共享下载位置 |
| tools/requirements.lock.txt、environment_baseline.json、check_environment.py 及本轮文档 | 本轮新增，尚未提交 | 要提交到双方实际使用的分支后才可在另一台拉取 |
| build/、Objects/、Listings/、.o/.axf/.map/.crf/.dep 等 | 构建过程文件，不需入库 | 保持忽略，另一台重新生成 |
| .venv/、UL/.venv/ | 绝对路径绑定本机 Python | 在每台电脑新建；不要复制虚拟环境 |
| backups/、outputs/、tmp/ 中的重要备份/交付包/原始安装包 | 本机内容，普通 clone 不包含 | 按保留价值单独备份并登记校验值；不能当作可丢弃源码处理 |

语音样例与本机原始 SDK ZIP 对比，已确认以下 4 个现有文件内容不同，迁移 SDK 原包时必须同时带上这些定制：

- projects/offline_asr_alg_pro_sample/app/app_main/user_config.h
- projects/offline_asr_alg_pro_sample/project_file/makefile
- projects/offline_asr_alg_pro_sample/firmware/user_file/cmd_info/[60000]{cmd_info}.xlsx
- projects/offline_asr_alg_pro_sample/firmware/config.ini

该比较限于原 ZIP 已存在的样例非生成文件，不是整棵 SDK 的穷尽差异证明。本次另改了 SDK .gitignore。

SDK ZIP 为 123,605,094 字节，GCC RAR 为 96,134,818 字节，当前都在 tmp/。不要把整个 tmp/、解压后的工具链或所有未跟踪项直接 git add -A。固定包的 SHA-256 已记录在 tools/environment_baseline.json；确认家中可取到再宣称环境可完整恢复。

## 6. 本轮重建证据

在 build/handoff_audit/ 下，用 git archive 导出提交并在新目录中构建，没有借用工作区未跟踪的主控源码和旧目标文件。编译器是 ARMCC 5.06 update 6 build 750。RO/RW 来自各镜像的 map，RW 包含已分配的 ZI。

| 提交 | 镜像 | BIN 字节 | RO 字节 | RW 字节 |
| --- | --- | ---: | ---: | ---: |
| 63d1ea3 | klq_bootloader | 9272 | 9084 | 8728 |
| 63d1ea3 | klq_demo_app | 14868 | 14660 | 26248 |
| 89bd9ac | klq_bootloader | 9844 | 9644 | 8736 |
| 89bd9ac | klq_demo_app | 20412 | 20128 | 26448 |

89bd9ac 系统固件 SHA-256 为 ddbcb3c2128d60a477cb01c2a09c4373f7db448e293662d7ce1b2552ed7795f3，与 2026-09-23 实板记录完全一致，证明该主控提交可以重建同一系统镜像。

89bd9ac 引导 SHA-256 为 28088912e71d9b8245a73d4e1194d6322aa12486acbc2d116766cd45dfd43f8e。旧实板记录中的引导是 9828 字节；后续 protocol_poll 增加 256 字节读取预算也影响引导公共代码，本次重建为 9844 字节。该新引导本轮未烧录，不能把旧引导验证直接套用到它。

UL 自研源码与本机 Puya SDK 另复制到全新目录，排除了 .git、.venv 和旧 build 后，以 Debug 配置及显式 -ArmccBin 重建通过：BIN 5776 字节，RO 5724 字节、RW 2008 字节，SHA-256 为 d7a70796321163361234b76d3ccc1048d266fc24371292697bb352e33c227d48。这证明本机输入齐全，不代表它们已经存在于父 Git 仓库。

环境检查工具已验证：公司完整环境 0 个失败、5 个分支/未跟踪警告；对干净 Python 工作树检查主控/烧录依赖，严格模式 0 个失败、0 个警告。缺少编译器、在旧 main 要求 runtime 的两个反例均正确返回失败。pip check 通过。语音只核对工具、库及模型输入，没有在本轮重建整包。

以上验证仍使用公司机器已安装的编译器和 Python，不等同于已在家中全新操作系统完成安装测试。另一台至少需要通过环境检查并重新编译。

## 7. 两台电脑的日常接力

双方始终使用同一明确的工作分支。跨电脑不需要复制本机 worktree 路径。开发中间版本也应形成可解释的提交，写清未完成状态；本地 stash、未跟踪文件和聊天记录不能作为唯一交接。

开始工作时：

~~~powershell
git status --short
git branch --show-current
git fetch origin
git pull --ff-only
git log -1 --oneline
~~~

只在工作区干净且确认当前分支跟踪目标正确时 pull；若两边发生分叉，先保留各自提交再合并/变基解决，不强推覆盖另一台成果。当前 main 的语音修改需先保存，不能直接在这个脏工作目录切换最新分支。

结束工作时：

1. 更新需求/接口/架构中的已确认变化；在本文记录实际分支、已完成项、未完成项、验证命令和结果。
2. 查看 git status 和 git diff；逐项暂存本轮源码、配置、库输入和文档，检查 git diff --cached，确认无嵌套仓库遗漏和意外大文件。
3. 编译相关目标，记录 BIN/map 大小；有硬件改动时另记硬件实测，未测部分明确写出。
4. 提交并推送当前分支。
5. 用 git rev-parse HEAD 与 git ls-remote --heads origin <当前分支> 比较提交号；一致才表示另一台能够拉取这些提交。
6. 把必要安装包和不可再生板载备份放到双方可访问的位置；只记录位置、版本和校验值，不把凭据写进仓库。

推荐先完成的合流顺序：保存当前 main 的语音工作；以已推送 Python 分支为主控基线；加入本轮交接材料与环境工具；独立纳入 UL 和其固定依赖；整理语音输入及可复现构建，再做功能合并。合流后把本文第 1 节改为唯一共享分支的实际提交。

## 9. 历史 HK32 电机子板第一阶段（2026-09-24，尚未提交）

**本节为旧 HK32 板记录。2026-10-26 用户改用 PY32F002BF15U6TR 并取消码盘，当前实现见本文末尾的“电机板更换 PY32”记录与 MOTOR/README.md。以下 SWD 烧录和无命令状态测试仅适用于历史 HK32 板，不能作为新板验证。**

用户确认外接电机 MCU 为 `HK32F030MF4U6`；PA3=TX、PB4=RX、PB5=SWCLK、PD5=SWDIO、PD1/PD2=码盘双路数据、PD3/PD4=同一颗 DRV8837 的 IN1/IN2、PD6=低有效红外发射，DRV8837 nSLEEP 直接拉高。截图保存在 `MOTOR/docs/board_schematic_20260924.png`。详见 `MOTOR/README.md`，其中记录分区、协议、构建命令、哈希和板测清单。

本轮已用 `F:\HK32` 的 HK32F030M 厂商模板及 ARMCC 5.06 update 6 build 750 构建寄存器级串口引导和电机应用，未连接或访问任一 DAPLink。命令为 `& MOTOR/tools/build_boot.ps1`、`& MOTOR/tools/build_app.ps1`、`python MOTOR/tools/pack_factory.py`，均通过。map：引导 Flash 4084/4608 B、RAM 1472/2048 B；应用 Flash 2780/11648 B、RAM 1448/2048 B。首次 SWD 工厂镜像 `MOTOR/firmware/factory/build/motor_factory.bin` 为 16384 B，物理下载基址 `0x08000000`，SHA-256 `51d0989ca8b7dd87a83e86029d70a2d0286d23726d10cc723fc30513b76406b0`；配套 manifest 记录应用 CRC32 `0x0DC5AB24`。工厂镜像包括末页有效记录，不能仅分别写 boot/app 两个 BIN。构建目录目前按 `MOTOR/.gitignore` 不纳入 Git，另一台须有相同 SDK/编译器并重建，或明确转交镜像及哈希。

`update.c` 的顺序写入、重试、断电无效化与 CRC，以及 `motor_safety.c` 的 250 ms 失联停机已有主机模拟测试；实板 Flash 擦写、USART 通信、向量跳转、PWM、码盘和电机安全尚无验证。应用提供串口 `SET_PWM/GET_STATUS/STOP/ENTER_BOOTLOADER`；码盘目前仅返回原始 PD1/PD2 电平，没有速度闭环。主机端升级器尚未接入。电脑有两个 DAPLink：原探针仍连科乐机，新探针尚未接电机板。用户要求先编译、连接后再下载；下载时必须确认探针序列号和目标型号、先备份原 Flash/选项字节、只对新探针操作并读回。当前 `main` 分支与原有其它未提交改动未切换、未合并、未推送。

烧录尝试更新：用户现已接好电机板，并断开主机 DAPLink；剩余探针枚举为 `jixin.pro CMSIS-DAP_LU`，UID `LU_2022_8888`，USB VID_C251/PID_F001、虚拟串口 COM121。用户明确要求**跳过 Flash 备份**，此前备份步骤不再适用。pyOCD 0.45.1 加载本机航顺 HK32F030M DFP 1.0.7，能列出 `hk32f030mf4u6` 目标；以该 UID 在 1 MHz/halt、100 kHz/under-reset、10 kHz/halt 三次只读连接均得到 `DebugPortSetup: SWD/JTAG communication failure (No ACK)`，未读到 CPUID/器件 ID，故**尚未擦写或烧录**。用户称板已供电、J3 线已逐一核对，但 NRST 未接探针，因此 under-reset 尝试不能作为有效复位下连接验证。已请用户实测 MCU VDD、NRST 对 GND 电压及公共地导通；待确认后再接读、确认目标、烧录工厂镜像并读回。pyOCD 命令出现错误文本时退出码仍可能为 0，必须核对输出本身。

后续诊断：用户实测 MCU VDD≈3.3 V、NRST≈3.3 V、探针与板 GND 导通。pyOCD 可通过 CMSIS-DAP 直接读取探针引脚位，SWCLK/SWDIO/nRESET 均呈高电平，但这不能证明探针到 MCU 引脚的导通。已请用户将板上 NRST 接至 DAPLink RESET/nRST，以进行真正的 under-reset 连接；等待接线后重试。若仍 No ACK，需逐一量测探针针脚至 PB5、PD5 的导通和是否交叉，以及目标运行时是否占用 SWD。

用户表示板上 NRST 不便引出，故暂不进行真实 under-reset 连接。已请用户断电测量探针 SWCLK→MCU PB5、SWDIO→MCU PD5 的导通，排除两线交叉与 PB5/PD5 短接，随后重新上电；等待结果。测量前后均未烧录。

用户重新连接后 SWD 恢复：仅有 UID `LU_2022_8888` 探针；HK32F030MF4U6 DFP 1.0.7 显示 Flash `0x08000000..0x08003FFF`、RAM `0x20000000..0x200007FF`，CPUID `0x410CC200`、MCU UID `10050618-3D2B1829`。主 Flash 读数 `0xAAAAAAAA`，RDP 选项原值 `0x00FFFF00`、FLASH_OBR `0xFFFFFFFA`，依航顺用户手册 V1.8 第 6–7 页为出厂默认 RDP Level 1；调试模式不能读写主 Flash。用户已明确跳过备份。手册说明 Level 1→0（RDP=0xAA）会自动整片擦除。曾试做指定探针/UID 的调试 AP 选项字节修改工具，但执行失败后已删除。初次尝试在 OPTWRE 校验前停止、未擦除；显式 halt 后两次选项字节擦除与 RDP 编程命令未报 WRPRTERR，但紧接着读回仍是旧值。软件 reset 后 FLASH_OBR 仍显示 Level 1，读取部分 Flash/选项地址出现 transfer fault。此处为历史过程记录，后续成功解锁与烧录见下文。

最终烧录记录：在用户明确允许丢弃原程序后，先用 pyOCD 芯片擦除及写入尝试验证 Level 1 阻断写入，首个页返回 `flash program page failure (0x1)`。直接通过 SWD AP 修改选项寄存器会在写 `FLASH_AR` 时使目标进入 LOCKUP，已删除该失败工具。按航顺 Flash 驱动顺序编译 `MOTOR/tools/ram_unprotect.c` 为 316 B SRAM 程序，`MOTOR/tools/run_ram_unprotect.py` 用指定探针和 MCU UID 校验后载入、运行；执行状态达到 `0xA006`，SR=`0x20`、RDP=`0x55AA`，软件复位后 OBR=`0xFFFFFFF8`（Level 0）、主 Flash 可读。该转换按航顺手册会自动整片擦除，用户要求跳过备份。

首次连续 BIN 烧写后读回发现，芯片未编程页不保证为 `0xFF`（9344 B 间隙读为以 `0x00` 为主的值）；引导、应用、有效记录本身均匹配。修正 `MOTOR/firmware/boot/boot_hw.c`：擦除后仅要求有效标记失效，且擦写须有 EOP；不再假设整页 `0xFF`。重建引导 `4116/4608 B Flash`、`1472/2048 B RAM`，应用仍 `2780/11648 B Flash`、`1448/2048 B RAM`。`MOTOR/tools/pack_factory.py` 新增只含真实内容的 `motor_factory.hex`（6912 B 有效数据，SHA-256 `197b87dcb3d8e39d820dd1e795690191a7ae4944ae60588a577355774a300ac4`）。指定 `LU_2022_8888` 用航顺 DFP 1.0.7 与 pyOCD 0.45.1 在 Level 0 下整片擦除并烧写此 HEX，独立读回：引导 4116 B SHA-256 `e2cf488fbd12569858d3b9be08a8acaf9b02fcd5264848c262a40b1da14ad103`、应用 2780 B SHA-256 `962fe41bca5e39f09e2acbd1db3334b3ba00db8b5e0d795eedac66f9be6d3759`、有效记录 16 B，全部逐字节一致。复位运行 1.5 s 后暂停检查：PC=`0x08001866`（应用范围）、`FLASH_INT_VEC_OFFSET=0x1200`、TIM1 CCR3/CCR4=0、PD6 输出高（红外关）；随后恢复运行。工厂连续 BIN 的占位 `0xFF` 不作为整片读回比较依据。当前 MCU 仅做无命令状态测试；UART INFO/控制/升级、实际电机 PWM、码盘和失联停机仍需接线实测。`MOTOR/` 与本交接仍未提交或推送，当前分支 `main` 提交 `63d1ea3`。

## 8. 每次交接的记录模板

~~~text
日期/开发电脑：
共享分支及已推送提交：
需求/接口变化：
本轮代码和依赖变化：
构建命令、工具版本、BIN/map 用量：
硬件型号、供电/接线、验证结果：
尚未验证的项目：
未提交/未推送内容及原因：
下一步具体任务：
额外制品的位置及 SHA-256：
~~~

开始新的 Codex 任务时可直接说明：“先读 README.md、docs/handoff_current.md 和 docs/development_setup.md，检查分支与工作区，然后按交接中的下一步继续。”在修改需求前核对当前分支正文版本，不用旧聊天中的进度覆盖较新的 Git 文档。

## 10. UL PA6 工作指示灯（2026-10-26）

用户新增 PA6 为高电平点亮的 LED，授权自行定义工作指示用途。本轮在 `main@63d1ea3` 工作区实现，未切换分支，保留主控/语音/电机已有改动。UL 固件版本从 `0x00010000` 升为 `0x00010001`（1.0.1），板型和串口协议布局不变。

- `UL/firmware/boards/ul_v1/board.c/.h`：PA6 预先清零，再配置低速推挽、无上下拉；通过 BSRR/BRR 原子置位/复位，不读改写整个 ODR，避免影响 PA5 TRIG。
- `UL/firmware/app/Src/main.c`：LED 由主循环非阻塞推进，每 100 ms 读取一次完整样本快照。正常每秒亮 100 ms；连续 5 次检测到 NO_ECHO、HW_FAULT 或样本年龄至少 200 ms 时改为 100 ms 亮/100 ms 灭，恢复一次正常检查后回到慢闪。TOO_CLOSE/OUT_OF_RANGE 按正常测量处理；未接主机不会单独触发告警。主循环卡死时 LED 保持最后电平。
- 引脚和行为已写入 `UL/超声波传感器固件架构_V0.1.md`、`UL/firmware/README.md`；协议文档同步当前 INFO 版本。原始 IO 工作簿尚未补录 PA6，此新分配以用户确认及板级代码为准。

使用本机 ARMCC 5.06 update 6 build 750，命令 `& UL/tools/build_sensor.ps1 -Configuration Release` 构建通过。修改前 BIN/ROM 5548 B、RO 执行区 5496 B、RAM 2008 B；修改后 BIN/ROM 5744 B、RO 执行区 5684 B、RAM 2016 B（含 1024 B 固定栈）。增量 ROM 196 B、RAM 8 B，应用 Flash/RAM 均未越界。新 BIN SHA-256：`af3747cabe36687d6ab9384d30203d75314f7b77b31cc1e5849eac94592c6b2e`，生成的 HEX/BIN 位于 `UL/firmware/app/build`。样本快照复制后重新读取当前毫秒数做无符号年龄比较，避免 ISR 更新样本与主循环旧时间戳之间产生误判，并支持毫秒计数回绕。

本轮 pyOCD 枚举结果为没有连接调试探针，未烧录任何 MCU；LED 实际极性/亮灭时序和主机通信回归尚待板测。下一步连接 UL PY32F002Bx5 的探针，重新核实目标和 UID 后仅烧录 UL 镜像，检查正常慢闪、回波持续异常快闪及恢复，同时核对测距/串口仍正常。UL 和本交接文件仍属于本机未提交内容，未提交、未推送；此前交接中的嵌套 SDK 入库问题仍未处理。

## 11. IR 双路红外反射传感器需求梳理（2026-10-26）

用户新增 `IR/` 作为双路红外反射传感器项目目录，目前原始资料只有 `IR/IR_io分配.xlsx`。已确认两路发射可独立开关，分别采集环境背景与开灯 ADC，以差分减小环境光干扰。IO 表指定 MCU 为 PY32F002BF15U6TR；左 ADC=PB1、左发射=PB3，右 ADC=PB0、右发射=PB2，两路发射均低亮；PA6 指示 LED 高亮；PA3 TX、PA4 RX、PA2 SWCLK、PB6 SWDIO。

普冉官方 Datasheet V1.8 第22～23页表3-2、第26页表3-4已核对：PB1=ADC_IN0、PB0=ADC_IN7，PA3 TX/PA4 RX均用AF1；F15U6为QFN20，上述ADC/UART与SWD分配未发现复用冲突。来源链接和封装脚号见 `IR/README.md`；仅手册核对，未证明实际PCB或模拟前端。不能套STM32的PB0/PB1 ADC通道编号。

用户进一步确认：地面黑白循迹与反射避障都要使用；左右分别输出数值，可以归一化；反射越强 ADC 越低，接收端无 RC 滤波，发射灯允许连续点亮；沿用 UL 串口协议，但主机必须区分传感器类型。因此反射量按“关灯 ADC - 开灯 ADC”计算，以有符号中间量避免负差值回绕；IR 使用独立类型身份和双路样本格式，由主机 INFO 识别，不能冒用 UL 超声波类型。

新增 `IR/README.md` 记录完整 IO、已确认要求、建议的左右错开采样流程和后续待办。采样延时、软件滤波、归一化范围/标定、刷新率、具体设备类型数值及双路协议字段没有定案。Python 分支现有 `klq.wait_ir(port, compare, value)` 无通道参数，双路接入需另定映射；当前需求是左右独立数值输出，未要求板端黑白/有无物体布尔判定。

本轮仍在 `main@63d1ea3`，保留其它已有工作区内容；只读取资料并补充文档，IO 工作簿未改，未创建固件、未编译、未连接或烧录设备。IR 与交接记录尚未提交、推送。下一步定义采样/归一化及独立类型接口，再创建 IR 独立工程；不得把 UL 已有构建或板测结果当作 IR 验证。

## 12. IR 固件 1.0.0 与主控接入（2026-10-26）

用户随后授权开始开发固件。已完成 IR 独立应用、板级驱动、异步双路 ADC 状态机、KLQ1 通信服务与构建脚本。仍在 `main@63d1ea3`，没有切分支或覆盖原主控语音改动；IO 工作簿与 UL SDK 未改。

- 左背景/左开灯/右背景/右开灯四阶段，每阶段等待 1 ms，丢 1 次 ADC 再平均 4 次，名义 10 ms 一对。PB1=CH0、PB0=CH7；差分为关灯减开灯，有符号保留，归一化为 ADC 满量程的 0～1000，无自动黑白标定。
- 只发布完整对。ADC 3 ms 转换超时或硬件失败立即关两灯，清零发布 HW_FAULT，100 ms 重试；校准等待有界。初始化/HardFault/RESET 均关灯，默认无看门狗/OTA；实际响应和光学性能未测。
- INFO：class=1、board=3（避开电机 board=2）、hw=1、type=2，版本1.0.0，GET_SAMPLE 数据28 B。主控按类型分别解析 IR/UL；IR轮询50 Hz、UL保留10 Hz；掉线清旧数据，日志/dashboard显示左右值。Python分支的双路API未接入，不能宣称积木可用。
- ARMCC5.06u6 Release：IR ROM6024/20352 B、RAM2040/3072 B（固定栈1024、堆0），BIN SHA-256 `665cebf3349df19f468c1ae5fba14a7f42c81c05f31cfc8f64e5131d2b2fac16`。HEX/BIN有效范围与内容核对通过，输出 `IR/firmware/app/build/ir_sensor_app.*`。
- 主控当前工作区独立目录重建：接入前ROM15988/RAM26384 B，接入后ROM16508/RAM26432 B，增量520/48 B；为避开旧构建文件不可覆盖，`tools/build.py`增加可选`--output-dir`。原语音改动保留；主控引导也回归重建通过，BIN9272 B。
- 实际C模拟测试通过：采样1190、服务/帧107、主控端口101次断言，另有CLI模拟UART单测10/10、dashboard解析及既有USB格式单测2/2；这些不是实板证据。复核修复了主控UART发送超时不解绑、会一直保留旧在线数据的问题，并做回归/临时移除修复的反例检查。详见 `IR/docs/verification_20261026.md` 和 `IR/docs/protocol_v1.md`。

本轮未访问硬件或烧录，未提交/推送；IR源码及文档、主控`external_ports.c/.h`、`main.c`、构建参数和dashboard修改均在本机工作区。下一步按IR验证记录安排目标/接线确认及首次板测，再调整稳定时间、标定与左右Python API。SDK默认借用既有UL固定副本，另一电脑需恢复相同依赖。

## 电机板更换 PY32、取消码盘（2026-10-26）

用户确认电机改用与其它传感器相同的 MCU，取消码盘；已读取 `MOTOR/motor_io分配.xlsx` 的 `Sheet1!A1:B7`，型号为 `PY32F002BF15U6TR`，PA0/PA1 分别为同一颗 DRV8837 的 IN1/IN2，PA3 TX、PA4 RX、PA2 SWCLK、PB6 SWDIO。IO 表无红外发射或 LED。表 SHA-256 `4dbc8d4754d40aad11e8ceafbbc4769e00bf055bb2446661995389b2576985d9`。nSLEEP 沿用此前直接拉高的确认；新板复位电平尚未测量。

当前默认 `MOTOR/firmware` 和构建/打包脚本已移植到 PY32。旧 HK32 源码、工具、测试、工厂产物及说明完整保存在 `MOTOR/legacy/hk32/`，其工厂 HEX 原哈希仍为 `197b87dcb3d8e39d820dd1e795690191a7ae4944ae60588a577355774a300ac4`。旧 HK32 RDP 工具已移至历史目录。当前 MCU Cortex-M0+、24 KiB Flash、3 KiB RAM，PA0/PA1 使用 TIM1 CH1/CH2 AF2，20 kHz；USART1 PA3/PA4 AF1、115200 8N1，保留 SWD。默认双低、正负 PWM -1000..1000、切换先归零、250 ms 失联停机；移除码盘/红外发射回调，GET_STATUS 的原码盘字节改为 reserved=0，长度仍8 B。软件不提供实测转速、圈数或位置；未启用独立看门狗。

身份 class=2、board_id=2、hw_compat=2、hw_revision=2、firmware=2.0.0；应用能力位只保留 PWM。新分区为 boot 0x08000000/6144 B、app 0x08001800/18304 B、有效记录页 0x08005F80/128 B。应用在开中断前设置 VTOR；跳转使用无栈汇编设置 MSP 后 BX，清理 SysTick/NVIC。串口引导沿用 KLQ1、500 ms HOLD 窗口、BEGIN/DATA/END/RUN，但 DATA 改为128 B整页、offset页对齐，最后一包允许偶数字节余数，尾部补FF。完全一致的重传只比较读回；擦写/校验失败需重新 BEGIN。有效记录先擦、镜像整包CRC/向量检查后最后整页提交；擦写使用普冉厂商24 MHz HSI时序和PGSTRT流程，禁止串口触及引导或选项字节。

依赖借用既有 UL 的固定 SDK 提交 `394627d99085c5eae72142503b0405b84428bb20`，ARMCC5.06u6 build750；未改动 UL/IR 或主控源码。命令 `& MOTOR/tools/build_boot.ps1`、`& MOTOR/tools/build_app.ps1`、`& .venv/Scripts/python.exe MOTOR/tools/pack_factory.py` 均通过。最终 map：boot ROM4260/6144 B、RO4240 B、RAM1600/3072 B；app ROM2840/18304 B、RO2820 B、RAM1448/3072 B，均含固定栈1024 B、堆0。boot SHA-256 `71915f2762bbe0557552d349545fa99c96b0fd1a0f951c3869cabd7b3b4a75ea`；app SHA-256 `93120b3c151c856482f62e7cb6a4f51152c4c79a49ad7d5bc678197ac74a57ad`。当前首次烧录 `MOTOR/firmware/factory/build/motor_factory.hex`，有效内容7116 B，SHA-256 `1b7750600910832db06dda6cfa2d44ada36e0fd37d168c26888e22c7502b6077`；连续BIN24576 B，SHA-256 `49877db81c7a0169d237a8bef42d777d558f5b31e44b1669e3c48a2631c5cdb9`，app CRC32 `0x9E7CD060`。

`& MOTOR/tools/test_motor.ps1` 使用VC98编译实际C安全/升级内核并运行，均PASS。覆盖正反转/零输出/非法PWM、250ms边界、毫秒回绕；升级覆盖页限制、错板/旧兼容号/超长/奇数长度、乱序、重复BEGIN/DATA/END、写入和提交失败后重新开始、CRC/栈错误及重启无效状态。工厂HEX记录校验和、地址、BIN一致性、身份/向量/CRC另作静态核对。以上均为软件证据，没有访问探针或烧录新板。

继续在 `main@63d1ea3`，保留已有未提交内容；执行期间其它任务修改了IR和主控端口/dashboard，未覆盖这些工作。MOTOR 和交接仍未提交/推送。下一步连接新电机板后重新核实探针、PY32目标/UID/供电，烧录当前工厂HEX并回读，再测INFO、PWM/轮向、STOP/失联、复位电平、串口升级及升级断电恢复。主控电机控制/串口升级器尚未接入。详情与协议见 `MOTOR/README.md`。

## 14. sensor_boot 三类 PY32 共用引导（2026-10-29）

用户要求在 `sensor_boot` 创建适用超声波、红外及电机的通用引导，便于后续由主机升级。已核对本交接、开发环境、当前源码/IO记录及固定SDK。三类新板均是PY32F002BF15U6TR，USART1统一PA3/PA4，SWD保留PA2/PB6；电机仅PA0/PA1，无码盘。此次授权为源码开发、构建及软件验证，未操作真实设备。

**已完成：** `sensor_boot/src` 下的共用引导、编译期ul/ir/motor板型、安全GPIO、Flash页写入、KLQ1升级/布局查询；`tools/build.ps1` 一次构建六个镜像并调用打包器；`tools/pack.py` 生成工厂HEX/BIN、升级容器、SHA-256 manifest及ZIP；`tools/update.py` 提供PC直连UART的身份/UID核对、复位进入/HOLD、逐页下载、丢应答重传、同上电续传、提交及应用INFO确认。统一协议见 [protocol_v1.md](../sensor_boot/docs/protocol_v1.md)。

**确定的实施边界：** 同一源码按三种板型生成工厂镜像，由首次SWD安装选定身份，不运行时猜测板型。使用主Flash低地址引导6144 B，应用0x08001800/18304 B，末页0x08005F80/128 B为有效记录，覆盖旧UL架构的末尾Load Flash规划；未修改选项字节。应用向量后0xC0处固定16 B KID1身份，打包/END/启动均验证板型、兼容号、基址、ABI，END/启动验证整包CRC。BEGIN先擦有效记录，顺序整页写且读回，最后提交记录。无堆、固定缓存/1024 B栈、无旧镜像回退或签名。普通复位500 ms窗口，ENTER应答后复位，主机须及时HOLD；未实现RAM令牌或独立看门狗救援。

**配套应用：** UL/IR的main在SystemInit后重设VTOR并启用中断，服务增加仅引导构建模式可用的ENTER_BOOTLOADER及升级能力；IR进入前关闭发射。UL reset响应确实入队后才复位，并在reset_pending期间抑制普通命令。电机保留停机/250 ms失联逻辑，升级前停机，配套应用增加升级能力位。版本仍为UL1.0.1、IR1.0.0、电机2.0.0；统一构建capabilities分别15、15、9。三份应用构建脚本新增输出目录及引导模式；UL也支持显式SDK路径。原独立应用/电机引导及HK32历史源码、产物未删除，主控/语音已有工作未覆盖。

**最终构建证据：** ARMCC5.06u6 build750、Python3.12.14、固定PuyaSDK `394627d99085c5eae72142503b0405b84428bb20`，SDK工作区仍干净。命令 `& sensor_boot/tools/build.ps1` 通过。UL引导ROM4464/RAM1600 B、应用ROM5776/RAM2016 B；IR引导ROM4468/RAM1600 B、应用ROM6052/RAM2040 B；电机引导ROM4456/RAM1600 B、应用ROM2856/RAM1448 B。各镜像链接范围和3 KiB SRAM均未越界，应用身份增加16 B RO，引导身份复核约增加104 B ROM、RAM无增量。完整哈希、CRC、地址、map读数固化在 [build_evidence_20261029.json](../sensor_boot/docs/build_evidence_20261029.json)，验证说明在 [verification_20261029.md](../sensor_boot/docs/verification_20261029.md)。

**软件验证：** 共用C内核、实际引导帧解析/命令在三种身份下通过，UL/IR实际服务进入引导/TX排空/毫秒回绕测试通过；Python工具13/13通过，包括对三个实际C引导实例传本轮整包应用、错板/内部身份、坏包、丢应答、续传、UID/布局/offset保护与工厂文件核验。既有IR采样1190、协议107、主控端口101次断言、UART工具10/10回归通过；电机安全与独立升级内核回归通过。原独立构建另输出到build/sensor_boot_regression，UL/IR/电机均可编译。没有访问探针、真实COM口、GPIO或Flash。

**当前交付入口：** `sensor_boot/build/{ul,ir,motor}/release/`。首次安装使用对应 `*_factory.hex`；其包含引导、配套应用及有效记录。UART只发 `*_update.klq` 容器中的原始应用，不能把工厂BIN当升级包。每目录有manifest与release.zip，构建目录仍忽略，需要同输入重建或单独转交ZIP。其他电脑仍需恢复既有嵌套PuyaSDK和ARMCC；本轮没有解决父仓库SDK入库问题。

**接续工作：** 先核实实际探针/目标/UID、供电与接线，分别安排新工厂镜像烧录及回读；验证安全电平、默认启动、VTOR/中断、测距/双路采样/PWM及250 ms失联、真实UART更新与各阶段断电恢复。APM32主控USB→外接端口升级转发尚未实现，需按统一协议增加端口排他升级状态机、暂停/恢复普通轮询和PC分包入口；当前PC工具只支持直连UART，不能宣称已经可经科乐机主机更新。

本轮仍为 `main@63d1ea38f69fbd478f92751855f238340b6fa853`，未切分支、未提交、未推送。正式跨电脑交接前须整理本轮及此前未提交输入，选择共同分支提交/推送并实查远端HEAD；不能把本机ZIP或聊天视为已同步成果。

## 15. 主控 USB→外接端口升级转发（2026-10-29，同日后续）

用户明确要求继续接入主控转发，让电脑通过科乐机更新超声波、红外和电机。已在当前main工作区完成；第14节“转发尚未实现”的表述为前一阶段状态。没有操作真实COM、USB设备、探针或板卡；未烧录、未切分支、未合并python-runtime、未提交或推送。

**实现入口：** `bootloader/external_ports.c/.h` 包含单会话非阻塞转发，`protocol.c/.h` 增加USB OPEN/XFER/RESULT/CLOSE 0x40～0x43，`main.c` 抑制会话期间非请求日志，`usb_serial.c` 发送等待继续推进UART并检查USB配置epoch。主控应用标记0.3，INFO声明EXT_UPGRADE=1、EXT_PORTS=3。没有新增主控构建文件或SDK依赖；引导构建仍不链接UART应用模块。电脑端 `sensor_boot/tools/update.py` 新增 `--via-host --sensor-port 1..3`，`host_link.py` 包装USB会话，保留直连UART。接口/使用方法见 [usb_bridge_v1.md](../sensor_boot/docs/usb_bridge_v1.md)。

**确定的行为：** OPEN锁定端口和随机token，停止用户程序、清旧样本、取消目标端口普通重试并排空50 ms；其它端口照常轮询。固定缓冲只存一个UART请求和响应，无整镜像缓存或新增堆分配。有效INFO锁定板型/兼容号/类别/类型/UID；ENTER/RUN及身份错误后需重新核对同UID的引导INFO和布局才允许擦写。主控对电机先STOP确认，再ENTER，自动衔接HOLD；ENTER应答丢失也执行HOLD，初次引导INFO同样先HOLD再返回电脑。BEGIN10秒×3重试、其它阶段各有有界预算；相同原帧幂等，确认超时后新序号可重试，同上电同包支持续传。

**独占与取消：** 会话期间拒绝用户程序BEGIN/DATA/END/RUN/DISPLAY/FINISH，STOP仍可用。关闭、USB复位/重新配置、30秒无正确token请求或显式主控RESET取消会话并重新发现端口，不恢复旧程序/电机动作。电脑失败和KeyboardInterrupt尝试CLOSE；重复CLOSE可应对丢ACK。USB RESULT不缓存瞬态结果；USB接收每轮256 B，发送等待持续服务UART。已复现并修复响应过程中USB复位后旧会话仍处理新队列请求的漏洞；收发按reset/config/overflow代次处理。

**最终资源：** 用修改前源码快照 `build/sensor_bridge_baseline` 重建基线后，应用从ROM16508/RAM26432 B变为ROM19760/RAM26720 B，增量3252/288 B；RO19528 B。主控引导从ROM9272/RAM8728 B变为ROM9388/RAM8728 B，增量116/0 B；RO9196 B。所有链接范围通过。主控仍保留原启动文件2048 B栈和512 B堆预留，桥接代码无堆分配。三类PY32工厂和应用镜像本轮未改。

**验证：** 实际桥接C内核161次断言、实际USB协议层300次断言通过。Python共30/30通过（原工具13、新USB工具13、完整链路4），无跳过。完整链路为电脑工具→实际主控C USB解析→实际主控C端口桥接→实际板端C引导；对三种生成应用完成END/CRC提交，另测错板、首个页后USB复位续传、DATA应答耗尽后的清理与重连续传。原生模拟不执行ARM应用，所以完整链路保持引导；ENTER/RUN及丢ACK由状态机单测覆盖。既有IR采样1190、协议107、端口101和主控格式2/2回归通过。详细命令和限制见 [verification_20261029_usb_bridge.md](../sensor_boot/docs/verification_20261029_usb_bridge.md)。

**产物：** 主控应用 `build/usb_sensor_bridge/app/klq_demo_app.bin/.hex`，BIN19760 B，基址0x08008000，有效末地址0x0800CD2F，SHA-256 `da1c5428fde35a4ffdf9578d05a8504e6b6ac76caa76bd510df4f46a60b1d1d7`。引导回归BIN9388 B，SHA-256 `2778253c659facc74eb16c842b2adb8525de09060a58ee57451f0c23d0fc7546`。HEX逐记录校验和、范围、与BIN一致性均通过。可转交 `build/usb_sensor_bridge/release/klq_usb_sensor_bridge_0.3.zip`，61169 B，SHA-256 `a773168b848d9e8bee6e633faba794732d8089a482ad592374a9dbaf43204a3b`；包含主控应用、升级工具/依赖清单、三个升级容器及manifest。完整源文件和镜像哈希在 [usb_bridge_build_evidence_20261029.json](../sensor_boot/docs/usb_bridge_build_evidence_20261029.json)。build仍忽略，跨电脑需另传包或重建。

**下一步：** 先确认当前实板主控版本及要保留的功能分支，再安装目标主控应用和对应外接板工厂引导；本包基线为main，不能视作已包含另一python-runtime分支功能。真实USB枚举/三个端口、UL测距/IR采样/电机停机、长BEGIN/USB延迟下自动HOLD、拔插/复位/升级各阶段断电、默认RUN后业务恢复仍待实板验收。正式Git交接前还需统一工作分支，整理既有未提交内容、固定嵌套SDK依赖，提交/推送后核对远端HEAD。

**同日跨对话同步：** 用户已明确确认发送同步要求，现已通知UL、Motor、IR三个对话核对并沿用现有APP重定位、VTOR、KID1身份、ENTER_BOOTLOADER和统一构建入口，发现实际缺口才最小修正。各对话只核对所属固件，分别在自身docs下记录，避免并发覆盖共用引导、主控及本交接文档；未授权烧录、切分支或推送。UL/IR独立应用默认仍从0x08000000启动，可升级交付必须由sensor_boot/tools/build.ps1 -Device ul/ir/motor选择统一引导模式。消息已送达，核对结果由各对话另行给用户报告。

**主控对话接续通知（2026-10-29）：** 用户随后明确要求通知“KLQ主机”对话，把串口升级三类外接固件的功能纳入主控。消息已送达该对话（01a09f10-c2f3-7753-8e1a-e834563f67ae），要求先核对其最新主控分支/worktree；该对话此前报告0.8、codex/python-runtime及ff33f97，须实际确认，不能用main的0.3桥接演示基线整文件覆盖解释器、积木、停止/电源等较新功能。已同步现有桥接代码/协议、自动STOP/HOLD、UID与布局门禁、独占/取消、测试命令和未做实板的边界，要求完成真实合流、重建、map与业务/升级回归并在主机对话向用户报告。UL/Motor/IR核对记录已在各自docs下；UL及Motor有小修复和新产物，旧主控合集ZIP中的副本已不是最新，最终应重新收集三类当前manifest/升级容器再打包校验。共用引导RUN发送/TC超时边界仍按Motor记录保留为待处理项。本次通知未授权实板烧录。
