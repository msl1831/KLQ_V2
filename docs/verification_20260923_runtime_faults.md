# RAM 用户程序与解释器容错验证（2026-09-23）

系统固件 0.7，APM32E103RET6 实板，经机器人 USB CDC 下载；DAPLink 提供调试供电。本轮未改引导镜像。

| 镜像 | 大小 | SHA-256 | 链接映射 |
|---|---:|---|---|
| `build/klq_demo_app/klq_demo_app.bin` | 20412 B | `DDBCB3C2128D60A477CB01C2A09C4373F7DB448E293662D7CE1B2552ED7795F3` | RO 20128 B；RW 26448 B |

解释器仍使用一块 16 KiB 固定 RAM 缓冲，无堆分配。完成、错误和停止后保留已下载文本，状态回到 READY，可重新 `RUN`；新 `BEGIN` 开始覆盖旧缓冲。复位或断电清空 RAM。错误由固件统一处理，`try/except` 不属于当前 Python 子集。

解释器每次主循环最多执行一条用户语句，并把同一次调用中的空行跳过、循环回跳等内部步骤限制为 32 次。用尽本次额度只让出 MCU 主循环，下次从当前程序位置继续，不视为错误或循环总次数限制。USB 协议每次主循环最多读取 256 字节，以便按键和系统服务持续得到处理。

## 实板结果

- 固件通过 USB 下载、整包 CRC 与有效镜像验证，启动后 `INFO` 报告 `KLQ ROBOT FW 0.7`。
- `tools/test_python_runtime.py`：正常结束、等待、有限循环、无设备等待可停止、语法错误、未知 API、不支持设备、错误行号、错误后同一程序再次运行、带等待及不带等待的无限循环 USB `STOP`、运行中 `BEGIN` 替换、USB 客户端关闭后自然结束均通过。
- 不带等待的 `while True:\n    pass` 在板上持续运行。用户短按 PC4 一次后，`INFO` 返回 `USER_STATE=2, USER_VALID=1, USER_LENGTH=34, UI=1, PY_ERROR=0, PK_SHORT=1`，说明实体键退出循环且程序仍在 RAM。
- 在已有 112 B 完整 RAM 程序处于 READY 时，用户保持 DAPLink 供电，仅拔插机器人 USB。重新连接后 `INFO` 返回 `USER_STATE=2, USER_VALID=1, USER_LENGTH=112, USER_RECEIVED=112, UI=1, USB_RESETS=4`；已完成的程序未因 USB 总线复位被清除。

外接电机和红外协议尚未接入；相关调用会报告不支持，不能据此声称实际电机已经收到停止指令。观测页由 `tools/status_dashboard.py` 提供，`/` 显示主机、端口、SC7 状态，`/faces` 是六种 7×13 待机表情候选；候选页不会改变板上图案。观测服务独占 USB CDC 串口，外部上位机需要直连该 COM 口下载时应先停用观测服务。
