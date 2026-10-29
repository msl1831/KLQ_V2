# KLQ + CI1303 语音联调固件

## 接线

- CI1303 PA2 / UART1_TX -> KLQ PC11 / UART4_RX
- CI1303 PA3 / UART1_RX <- KLQ PC10 / UART4_TX
- 两板 GND 共地，串口均为 3.3 V 推挽电平
- CI1303 PC4 -> 功放 EN；低电平播放，高电平休眠
- 串口：115200 bit/s，8 data bits，no parity，1 stop bit

## 当前测试行为

1. CI1303 上电播放提示音 ID 400。
2. KLQ 上电 5 秒后通过 UART4 发送一次“按命令 ID 播放”请求，命令 ID 199 对应“好的”。未收到 `0x80` 播放 ACK 时，每 1.5 秒重试，最多 3 次。
3. 当前临时联调包使用 SDK 默认唤醒模型“智能管家”，唤醒回复为提示音 ID 199。后续取得模型后再切换为“小乐小乐”。
4. CI1303 唤醒后发送官方 V2 协议通知 `type=A3, cmd=9A, data=00 B1`；KLQ 将 `WAKE` 计数加一。
5. CI1303 的普通识别结果使用 `type=A0, cmd=91` 上报；KLQ 保存语义 ID/分数并回复 `type=A2, cmd=91, data=00`。

KLQ 测试播放请求的完整帧为：

```text
A5 FC 06 00 A1 92 00 80 92 C7 00 00 00 0C 03 FB
```

USB 调试串口会在状态变化时输出：

```text
VOICE RX=<帧数> FE=<帧错误> UE=<串口错误> WAKE=<唤醒次数> ASR=<语义ID> SCORE=<分数> NOTIFY=<事件> PLAY_ACK=<播放应答> TEST=<发送次数>
```

正常收到测试播放请求的应答时，`PLAY_ACK=80`。当前版本识别“智能管家”后应看到 `WAKE` 增加且 `NOTIFY=B1`。

## 构建结果

- KLQ 测试应用：`build/klq_demo_app/klq_demo_app.bin`，15,988 bytes
- KLQ Bootloader：`build/klq_bootloader/klq_bootloader.bin`，9,272 bytes
- CI1303 代码镜像：`projects/offline_asr_alg_pro_sample/firmware/user_code/[0]code.bin`
- CI1303 代码用量：text 76,734 bytes，data 1,452 bytes，bss 115,404 bytes
- CI1303 默认模型联调整包：`outputs/01a0a414-f2f5-7590-a85a-b75445a3c981/KLQ_CI1303_DEFAULT_ASR_TEST_0.1.0.bin`，2,002,968 bytes

CI1303 工程已按 1303 芯片、D03GS01J 引脚、UART1 和精简播放器完整编译通过。`[0]code.bin` 只是代码分区；可下载文件应使用打包后的完整固件。

## 尚缺的最终资源

临时联调包使用 SDK 自带的 `lm_chinese_CI1306_V00874.dat` 默认语言模型，以便先验证上电播报、UART4 控制播放、唤醒通知和 ASR 结果回传。模型文件来自官方 CI13XX SDK 示例，但文件名标注 CI1306，因此此包仅用于 CI1303 样机联调，不作为量产版本。量产前仍需在原厂 AI 平台生成 CI1303 的“小乐小乐”模型并重新打包。
