# 传感器与电机共用引导

2026-10-29：支持现有 PY32F002BF15U6TR 超声波、双路红外和无码盘电机板。共用一套引导源码，按 `ul`、`ir`、`motor` 生成各自工厂镜像。硬件没有自动板型识别入口，首次 SWD 安装时须选择对应板型。

已完成引导、三种配套应用、打包器，以及 PC 直连 UART 和经科乐机主控 USB 转发的升级工具，编译及完整软件链路验证通过。**尚未烧录或实板验证。** 板端使用 [统一 UART 协议](docs/protocol_v1.md)，经主控升级的用法与外层协议见 [USB 升级说明](docs/usb_bridge_v1.md)。

## 构建

依赖 ARMCC 5.06 update 6 build 750、Python 3.12，以及现有 UL 的固定 Puya SDK `394627d99085c5eae72142503b0405b84428bb20`。SDK 仍是既有嵌套仓库，另一台电脑须按 [环境文档](../docs/development_setup.md) 恢复。使用官方 SDK 的 CMSIS 和启动文件，不复制整个 SDK。

在仓库根目录执行：

```powershell
& sensor_boot/tools/build.ps1
# 只构建一个板型：
& sensor_boot/tools/build.ps1 -Device ir
```

可通过 `-ArmccBin`、`-SdkRoot`、`-Python` 指定依赖路径；也支持 `KLQ_ARMCC`、`KLQ_PY32_SDK`。编译会检查引导 6144 B、应用 18304 B 和 RAM 3072 B 的链接上限。固定栈 1024 B，堆 0 B。

每个板型输出到 `sensor_boot/build/<板型>/`：

| 文件 | 用途 |
| --- | --- |
| `boot/sensor_boot.bin/.hex/.map` | 对应板型的引导及资源报告 |
| `app/*_app.bin/.hex/.map` | 从 `0x08001800` 链接的配套应用 |
| `release/*_factory.hex` | 首次 SWD 安装，含引导、应用和有效记录 |
| `release/*_factory.bin` | 同一内容的 24576 B 连续镜像，基址 `0x08000000` |
| `release/*_update.klq` | 后续 UART 升级包，只更新应用 |
| `release/*_manifest.json` | 身份、地址、长度、CRC32、各文件 SHA-256 |
| `release/*_release.zip` | 对应板型的工厂文件、升级包和 manifest |

首次安装使用对应 `*_factory.hex`，独立应用文件没有有效记录。历史电机引导和 HK32 工程仍保留；本目录的工厂镜像为统一引导入口。UL/IR 原构建命令仍生成从 `0x08000000` 启动的独立应用；调用它们的 `-Bootloader` 模式才使用本目录的分区和应用身份。主机升级包必须通过本打包器生成。

## 分区与行为

| 区域 | 地址 | 容量 |
| --- | --- | ---: |
| 引导 | `0x08000000..0x080017FF` | 6144 B |
| 应用 | `0x08001800..0x08005F7F` | 18304 B |
| 有效记录页 | `0x08005F80..0x08005FFF` | 128 B |

应用向量表占前 192 B，后面的 16 B 固定记录板型、硬件兼容号、链接基址和 ABI。打包器、升级 END 和每次启动都核对该身份；END 和启动还校验完整应用 CRC。不同板型或 HK32 兼容号在 BEGIN 阶段即被拒绝。

普通复位后开放 500 ms 握手窗口，收到 `BOOT_HOLD` 后保持引导；有效应用随后自动启动。应用收到 `ENTER_BOOTLOADER`，先应答并等待 UART 发送完成，再复位，主机须及时发送 HOLD。没有 RAM 引导令牌。应用损坏、更新中断或 CRC 错误时一直等待升级。软件复位不跨断电保存下载进度；同次上电、同一包可按 BEGIN 返回的 offset 续传。

BEGIN 先使有效记录失效，再擦应用。每包顺序写一个 128 B 页，末包允许偶数余数，尾部填 FF；同一页重传只比较读回。END 验证完整后最后提交记录；失败须重新 BEGIN。单槽设计不保留旧版回退。UART 路径没有引导区和选项字节写入口。

引导期间电机 PA0/PA1 均低，红外 PB2/PB3 均高以关闭发射，超声 PA5 TRIG 和 PA6 LED 均低；PA2/PB6 保留 SWD。电机两输入低为滑行停机，机械停止距离仍需板测。应用跳转清理 SysTick/NVIC/USART，设置 MSP/VTOR；UL/IR 在厂商 SystemInit 之后重新设置 VTOR，再启用应用中断。

本实现采用主 Flash 低地址常驻引导，不采用旧 UL 架构草案的末尾 Load Flash/选项字节方案。当前资源和 Flash 页布局参照 [普冉数据手册](https://www.puyasemi.com/download_path/%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C/MCU%20%E5%BE%AE%E5%A4%84%E7%90%86%E5%99%A8/PY32F002B_Datasheet_V1.8.pdf)及固定 SDK 的 Flash 实现；芯片擦写、VTOR 跳转和默认电平尚需实板确认。

## 直连串口升级

通过科乐机主控 USB 更新时，在下列工具命令增加 `--via-host --sensor-port 1`，其中 `--port` 填主控 USB COM 号、`--sensor-port` 填实际外接端口1～3。主控业务分支已合入桥接，运行带 `EXT_UPGRADE=1` 的系统固件0.9；详情见 [USB升级说明](docs/usb_bridge_v1.md)。旧main的0.3合集是历史基线，不含本分支的Python业务功能。

首次安装对应工厂镜像后，将兼容板上电平的 USB-UART TX 接 PA4、RX 接 PA3，并共地。串口为 115200、8N1；升级期间独占该 UART。

```powershell
# COM号按实际连接填写，以下仅示例：
& .venv/Scripts/python.exe sensor_boot/tools/update.py --port COM10 --image sensor_boot/build/ul/release/ul_update.klq
```

工具先验证包与设备身份，再进入并保持引导，核对 UID/分区，逐页写入和重传，END 后 RUN，最后以同 UID 的应用 INFO 确认启动。`--stay-in-boot` 可在提交后继续保持引导。包内身份检查和 CRC 用于防选错与损坏，未实现签名认证。

原独立 UL/IR 应用没有常驻引导，须先通过 SWD 安装本工厂镜像。若应用无法响应进入引导，可人工重新上电并由主机在窗口中发送 HOLD，或使用 SWD 恢复；当前没有独立看门狗救援或主机可控复位/电源。

## 验证

```powershell
& sensor_boot/tests/run_tests.ps1
& .venv/Scripts/python.exe -m unittest discover -s sensor_boot/tests -p test_tools.py -v
```

第一项使用现有 VC98 编译实际 C 升级内核、帧解析和 UL/IR 服务；第二项验证容器、工厂 HEX/BIN/manifest/ZIP，并通过模拟 UART 向三个实际 C 引导实例发送本轮完整应用。详细资源、哈希、回归和未测项见 [验证记录](docs/verification_20261029.md)。
