# 双路红外反射传感器

2026-10-29：新增 [共用引导模式](../sensor_boot/README.md)，统一分区、进入引导命令、升级包及主控USB转发已完成编译和软件链路测试；本页后续的无引导/NOT_SUPPORTED 描述适用于原独立应用构建。首次安装和后续升级使用 sensor_boot 输出。实板待验证。

更新：2026-10-26。独立传感器应用固件 1.0.0 已完成，构建及软件模拟验证通过；尚未烧录或做光学/ADC 实板验证。

## 已确认

- `IR/` 是新增双路红外反射传感器的独立项目目录。
- 左右两路发射灯可以分别开关；分别采集关灯时的环境背景 ADC 和开灯时的 ADC，用于减小环境光干扰。
- 用于地面黑白循迹和反射避障，主机需要分别取得左右两路数值；允许输出归一化后的值，归一化范围及标定方式尚未指定。
- 用户确认：反射越强，ADC 越低；接收端无 RC 滤波；发射灯允许连续点亮。
- 沿用 UL 外接传感器串口协议，主机必须能区分不同类型传感器；IR 的具体设备标识与样本格式另行定义。
- 下表来自原始 `IR_io分配.xlsx` 的 `Sheet1!A1:B10`；工作簿保持原样。

| 引脚/器件 | 功能 | 电平约定 |
| --- | --- | --- |
| PY32F002BF15U6TR | 传感器 MCU | 以 IO 表型号为准 |
| PB1 | 左路红外 ADC，ADC_IN0 | 反射越强，ADC 越低 |
| PB0 | 右路红外 ADC，ADC_IN7 | 反射越强，ADC 越低 |
| PB3 | 左路红外发射控制 | 低亮、高灭 |
| PB2 | 右路红外发射控制 | 低亮、高灭 |
| PA6 | 指示 LED | 高亮、低灭；指示用途待定 |
| PA3 | UART TX，AF1 | 沿用 UL 协议 |
| PA4 | UART RX，AF1 | 沿用 UL 协议 |
| PA2 | SWCLK | 保留调试用途 |
| PB6 | SWDIO | 保留调试用途 |

芯片能力已按普冉官方 [PY32F002B Datasheet V1.8](https://www.puyasemi.com/download_path/%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C/MCU/PY32F002B_Datasheet_V1.8.pdf)核对：第7页表1-1确认 F15U6 为 QFN20，第19页图3-1为引脚图，第22～23页表3-2和第26页表3-4给出上述 ADC/UART 对应。PB1、PB0、PA3、PA4 分别为封装第11、12、16、17脚，这几项与 SWD 分配未发现复用冲突；此为手册核对，不是 PCB 接线或 ADC 实测。不能套用 STM32 的 PB0=通道8/PB1=通道9：本芯片通道8/9用于内部温度/VREFINT。

## 首版采样实现

四阶段依次采左背景、左开灯、右背景、右开灯；每次只允许一路发射灯点亮。每阶段等待 1 ms，丢弃首次 ADC 转换，再平均 4 次。ADC 为 12 位、3 MHz、239.5 周期采样时间；转换异步轮询，不阻塞等待稳定时间或转换结束。

- 名义整对周期 10 ms（100 Hz），由 `IR_SAMPLE_PERIOD_MS` 指定；稳定时间由 `IR_SETTLE_MS` 指定。两者是首版工程参数，实际响应时间/刷新率仍待板测。处理过慢时不追赶补发历史周期。
- 差分为 `关灯 ADC - 开灯 ADC`，有符号保留负值供诊断；对外数值为 `round(max(差分,0)*1000/4095)`。0～1000 是 ADC 满量程比例，不是经过黑白标定的物理反射率或距离；没有自动追踪历史最大/最小值。
- 只有左右四项采集完整后才发布新序号/时间戳；通信读取静态快照，不能读到半对更新。
- 关灯或开灯读数不大于 8、差分为负时附带左右独立质量标志，整对状态为 DEGRADED，原始值仍保留。上端接近 4095 可能是正常弱光背景，不直接当作故障。
- ADC 转换超过 3 ms 或初始化/转换失败时立即关两灯，清零数值并发布 HW_FAULT；100 ms 后尝试重新校准恢复。校准、禁用与时钟等待都有界；未启用看门狗。
- GPIO 初始化先预装两灯关闭电平；HardFault、错误处理和 RESET 路径均关灯。PA6 正常每秒亮 100 ms，硬件故障或样本超过 200 ms 未更新时为亮 100 ms/灭 100 ms。

## 通信与主控接入

沿用 UL 的 115200 8N1、3.3 V TTL、KLQ1 头和 CRC32。IR 身份为 `device_class=1 / board_id=3 / hw_compat=1 / sensor_type=2`，固件版本 `0x00010000`。UL 超声波仍为 `sensor_type=1`；电机板已占用 `board_id=2`，没有复用其板号。

实现 INFO、ECHO、GET_SAMPLE、GET_STATUS、SELF_TEST、RESET。双路 GET_SAMPLE 数据为 28 字节，含序号、采集时间、左右差分/0～1000值、关灯/开灯 ADC、状态及质量标志。具体字段见 [IR 协议](docs/protocol_v1.md)。未实现引导升级，SET_CONFIG/BOOT_HOLD/ENTER_BOOTLOADER 返回 NOT_SUPPORTED，INFO 不宣称升级能力。

当前 `main` 工作区主控已按 INFO 区分 UL/IR，按各自长度解析，IR 每 20 ms 轮询一次（50 Hz），UL 保持原有 100 ms。USB 诊断日志和 `tools/status_dashboard.py` 分别显示左右数值；拔出/失联解绑时清除身份与旧样本。主控轮询频率、传感器后台采样频率和页面/日志刷新频率是不同指标。

本地 `codex/python-runtime` 分支的 `klq.wait_ir(port, compare, value)` 无通道参数；本轮未合并该分支或实现双路 Python 积木/API。这部分需在最新主控分支上另行接入，不能把当前 main 的识别/取样视为 Python API 已完成。

## 构建与软件验证

使用 ARMCC 5.06 update 6 build 750；SDK 固定为 `394627d99085c5eae72142503b0405b84428bb20`，默认读取 `UL/firmware/vendor/PY32F002B_Firmware`，不复制或修改 SDK。可用 `-VendorRoot` 或 `KLQ_PY32_SDK` 指定同版 SDK。

```powershell
& IR/tools/build_sensor.ps1 -Configuration Release
& IR/tests/run_host_tests.ps1
& .venv/Scripts/python.exe -m unittest discover -s IR/tests -p test_uart_tool.py -v
```

输出 `IR/firmware/app/build/ir_sensor_app.hex/.bin`。镜像从 `0x08000000` 启动，无独立 bootloader；链接限制 Flash `0x4F80`（20352 B）、RAM `0xC00`（3072 B），保留芯片 Flash 尾区。Release ROM 6024 B、RO 执行区 5952 B、RAM 2040 B（含固定 1024 B 栈、0 B 堆）。详细命令、哈希和验证边界见 [首版验证记录](docs/verification_20261026.md)。

串口检查工具需另接 PA3/PA4 和公共地；默认不会复位设备：

```powershell
& .venv/Scripts/python.exe IR/tools/test_sensor_protocol.py --port COMx
```

实际下载时必须确认目标为 IR 的 PY32F002Bx5、探针与镜像，不能使用主控 APM32 或电机 HK32 下载配置。本轮未执行下载。

## 本轮证据与后续顺序

- 工作区：`main@63d1ea38f69fbd478f92751855f238340b6fa853`，已有其它未提交内容，未切换或合并。
- 已完整读取 IO 表（1 张表、10 行、2 列，无嵌入图片），并核对现有交接及传感器接口资料。
- IO 表 SHA-256：`ebcaf275479596b466d820fb1eab90600e6da86ffbbd83144c8282fb236658c5`。
- 本轮完成 IR 固件、主控类型接入、构建与实际 C 源码模拟测试；未连接设备、烧录或进行光学/ADC 实测。IR 目录和本轮修改尚未提交、推送。
- 下一步实板确认：上下电默认灯灭、左右对应及串扰、ADC 稳定时间/采样率、强环境光饱和、黑白表面和障碍反射数值、串口/拔插/异常恢复；再决定标定参数和双路 Python API。
