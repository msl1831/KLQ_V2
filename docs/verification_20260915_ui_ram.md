# KLQ 显示状态与 RAM 用户程序验证记录

日期：2026-09-15

## 验证对象

| 镜像 | 大小 | SHA-256 |
| --- | ---: | --- |
| USB 引导 `klq_bootloader.bin` | 9272 字节 | `451d818f508f06fb1ac595132b10d1abd7f81fb3f1b8c10ba5b4a7c0b10b582f` |
| 机器人系统固件 `klq_demo_app.bin` | 13100 字节 | `917fd4ef65e74aa6fa70eb46b0c16c6377e6b7c1c35304194bd4423b763e294e` |

系统固件链接在 `0x08008000`。链接映射显示总 ROM 13100 字节、总 RW 25128 字节；其中 RAM 用户程序缓冲 16384 字节，位于 `0x2000197C`，当前静态 RAM 和栈远低于 APM32E103RET6 的 128 KiB 上限。一次性引导请求占用 RAM 顶部 `0x2001FFF8`～`0x2001FFFF`，不与链接区重叠。图标由文本行改为 13 字节静态列位图后，引导减少 320 字节，系统固件减少 264 字节。

## 已完成的实板验证

- 经 DAPLink 仅更新引导扇区，写入前备份全部 512 KiB 内部 Flash；随后逐字节回读 9272 字节，引导 SHA-256 与构建产物一致。
- 无一次性引导请求时由 DAPLink 触发普通复位，设备自动枚举为 `KLQ ROBOT FW 0.2`，证明有效系统固件自动启动。
- 系统固件收到 USB `reset` 后，设备重新枚举为 `KLQ USB BOOT 0.2`；引导收到 `run` 后重新进入系统固件。
- 引导侧完成 26 组 0～1028 字节 ECHO、重复请求重放、帧 CRC 拒绝、镜像长度边界和错误后恢复测试。
- 系统固件升级完成中断下载失效保护、错误整包 CRC 拒绝、完整写入与 CRC、运行期 ECHO、返回引导测试。
- RAM 用户程序拒绝超过 16 KiB 的长度；错误 CRC 会清除有效状态并进入错误显示；正确 CRC 会进入完成提示，800 ms 后返回待机。
- RAM 用户程序运行时进入默认运行显示；发送 13 列用户图案后进入用户显示；FINISH 和 STOP 都恢复待机。
- 复位后 `USER_VALID=0`、`USER_STATE=0`，证明用户程序有效状态不会跨复位保留。

核心实测命令：

```powershell
.venv\Scripts\python.exe tools\build.py
.venv\Scripts\python.exe tools\build.py --demo
.venv\Scripts\python.exe tools\dap_flash.py --verify-only
.venv\Scripts\python.exe tools\test_protocol.py --port COM123
.venv\Scripts\python.exe tools\test_update.py build\klq_demo_app\klq_demo_app.bin --allow-app-erase
.venv\Scripts\python.exe tools\test_user_program.py
```

## 显示状态定义

| UI 值 | 状态 | 点阵行为 |
| ---: | --- | --- |
| 0 | 开机 | 中心光点扩展、收拢，闭眼笑脸随后睁眼 |
| 1 | 待机 | 笑脸，每隔约 4.5～8.6 秒双眨眼一次 |
| 2 | 下载中 | 向下箭头与托盘 |
| 3 | 下载完成 | 对勾，800 ms 后回待机 |
| 4 | 运行中 | 默认三角形运行图标 |
| 5 | 用户显示 | Python 用户程序提交的 13 列图案 |
| 6 | 错误 | 叉号，800 ms 后回待机 |

USB INFO 对所有稳态和反馈状态的切换均已回读验证。点阵电气映射、下载图标和方向此前已经由用户观察确认；本次新增开机动画、待机表情、运行及完成图标的实物观感仍需观察确认。

## 当前边界

RAM 下载、CRC、运行控制和显示优先级已经形成，但精简 Python 解释器尚未接入。当前 RUN 只把已校验的 RAM 内容置为运行状态，不解析或执行其中的 Python 源码；DISPLAY 测试由 USB 命令模拟未来 Python 显示 API。

蓝牙用户程序下载、电源键与用户按键、串口外接设备协议、音频协议、看门狗和实体按键强制进入引导仍待实现。用户程序没有写入 MCU 内部 Flash；外部 GD25Q80ESIG 保持备用。
