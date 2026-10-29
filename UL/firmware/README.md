# UL 超声波传感器固件

2026-10-29：共用引导及配套应用已在 [sensor_boot](../../sensor_boot/README.md) 实现并编译验证，主控USB升级转发已接入并通过软件链路测试。后续升级从该目录构建工厂镜像和升级包；本页原构建命令仍为独立应用，下面的 NOT_SUPPORTED 描述适用于独立构建。尚未烧录实板。

本目录只用于 `PY32F002BF15U6TR + CS100A` 传感器板，与仓库根目录下的 KLQ 主机固件分开构建和下载。

## 首版范围

- PA0 / TIM1_CH1：CS100A ECHO 输入捕获，500 kHz 计数。
- PA5 / TIM14：50 us TRIG 定时脉冲。
- PA3 TX、PA4 RX：USART1，115200 8N1。
- PA6：高电平点亮工作指示 LED，固件版本 `1.0.1`。
- KLQ1：INFO、ECHO、GET_SAMPLE、GET_STATUS、SELF_TEST、RESET。
- 连续自适应测距：本次 ECHO 完成或 70 ms 超时后，间隔 1 ms 再触发。

本版本是应用区 bring-up 固件。`ENTER_BOOTLOADER` 和 `BOOT_HOLD` 当前返回 `NOT_SUPPORTED`，避免在 Load Flash 引导和选项字节尚未验证时产生虚假的升级能力。独立看门狗默认关闭，实板通信和测距稳定后再启用。

## 工作指示灯

PA6 初始化为低电平、低速推挽输出。正常运行每秒亮 100 ms；每 100 ms 检查一次采样状态，连续 5 次为 `NO_ECHO`、`HW_FAULT` 或样本达到 200 ms 未更新时，改为亮 100 ms、灭 100 ms 的 5 Hz 快闪。一次正常检查即恢复慢闪。`TOO_CLOSE` 和 `OUT_OF_RANGE` 不触发快闪；无回波只表示回波异常，不能凭 LED 判定具体硬件故障。

LED 由主循环推进，不增加定时器、中断或阻塞延时；主循环停止时灯保持最后电平，不会独立继续闪烁。LED 不表示主机已收到串口响应。

## 构建

在仓库根目录运行：

```powershell
powershell -ExecutionPolicy Bypass -File UL/tools/build_sensor.ps1
```

输出位于 `UL/firmware/app/build`。下载目标必须选择 `PY32F002Bx5`，不能使用 KLQ 主机工程的 APM32 下载配置。

## 下载与串口测试

DAPLink 只负责传感器 MCU 的 SWD 下载；DAPLink 虚拟串口只有在其 UART 引脚另行接到传感器 PA3/PA4 时才能测试通信。不要把 KLQ 主机串口或主机固件下载目标代入本目录脚本。

```powershell
powershell -ExecutionPolicy Bypass -File UL/tools/flash_sensor.ps1
UL/.venv/Scripts/python.exe UL/tools/test_sensor_protocol.py --port COMx
```

若板上已有需要保留的固件，烧录前应把备份放在 `app/backup` 等非 `build` 目录；构建脚本会清理自身生成的目标文件。
