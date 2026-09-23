# KLQ 上位机 USB 通信协议 V0.1

本文给上位机实现者使用，描述如何把积木生成的 Python 源码下载到机器人并控制运行。积木生成的函数名及参数见 [Python 积木接口](python_api_v0.1.md)。机器人内部的传感器轮询、串口事务和电机驱动不由上位机调用。

## 连接与运行环境

连接机器人自身的 USB 数据口，打开其 CDC 虚拟串口。开发板当前使用 `VID:PID=314B:0108`、115200 8N1；COM 号会变化，不应固定。一次只能有一个程序占用该串口，串口监视器与下载器不能同时打开。

打开后先发送 `INFO(1)`：返回文本以 `KLQ ROBOT FW ` 开头时，`BEGIN/DATA/END` 才表示下载 Python 到 16 KiB RAM；以 `KLQ USB BOOT ` 开头时，同样的命令会**擦写内部 Flash 中的系统固件**，此时不得发送用户程序。RAM 程序在 MCU 复位或断电后失效，不存入内部或外部 Flash。当前蓝牙下载协议尚未实现。

USB 字节流中可能混有 ASCII 调试日志。接收端须搜索 `KLQ1` 帧头并按序号、命令、CRC 识别应答，不能假定读到的每个字节都属于协议帧。

## 帧格式

所有多字节整数均为小端；头部固定 16 字节。

| 偏移 | 长度 | 内容 |
|---:|---:|---|
| 0 | 4 | ASCII `KLQ1` |
| 4 | 4 | `sequence:u32`，由上位机指定 |
| 8 | 2 | `command:u16` |
| 10 | 2 | `payload_length:u16` |
| 12 | 4 | `crc32:u32` |
| 16 | `payload_length` | 载荷 |

CRC-32 覆盖偏移 0～11 的 12 字节及完整载荷，算法与 `zlib.crc32(header12 + payload)` 相同，CRC 字段自身不参与计算。请求载荷上限 1028 字节。应答沿用该格式，命令号是请求命令号 `| 0x8000`，应答载荷前 4 字节为 `status:u32`，后续才是返回数据。上位机一次只发送一个请求，核对序号、应答命令和 CRC 后再发下一条。若未收到应答，可用**同一序号和完全相同的帧**重试；机器人仅缓存最近一条有效请求的应答，不支持断线续传。

## 用户程序下载与控制

| 命令 | 编号 | 请求载荷 | 成功应答附加数据 |
|---|---:|---|---|
| `INFO` | 1 | 空 | ASCII 设备状态文本 |
| `BEGIN` | 2 | `source_length:u32, source_crc32:u32` | 无 |
| `DATA` | 3 | `offset:u32, source_chunk:1～1024 字节` | `received_length:u32` |
| `END` | 4 | 空 | 无 |
| `RUN` | 5 | 空 | 无 |
| `STOP` | 9 | 空 | 无 |

下载步骤：

1. 根据 [积木接口](python_api_v0.1.md)生成源码，编码为无 BOM 的 UTF-8 字节，推荐 `\n` 换行。源码长度按**字节**计，范围 1～16384。
2. `INFO` 确认当前是 `KLQ ROBOT FW`。对完整源码计算 CRC-32，发送 `BEGIN`。若正在运行旧程序，`BEGIN` 会停止旧程序并开始接收新程序；下载期间点阵显示向下箭头。
3. 从偏移 0 开始，以最多 1024 字节一包顺序发送 `DATA`。每包偏移必须等于此前已接收的字节数；校验应答中的 `received_length`。
4. 发送 `END`。机器人核对长度和整包 CRC；成功后 `USER_VALID=1`，短暂显示完成图标约 800 ms，再回待机。失败时程序无效，需重新 `BEGIN`。
5. 需要立即运行时，等 `INFO` 返回 `UI=1`（待机）后发送 `RUN`；也可让用户在待机状态短按电源键启动。运行中发送 `STOP` 或短按电源键可停止并回待机。已下载程序停止或正常结束后仍可再次运行；复位后需重下。

USB 总线若在 `BEGIN` 与 `END` 之间复位，机器人取消半包 RAM 程序并回待机；上位机重新连接后应重新发送 `BEGIN` 和全部 `DATA`，不能从旧偏移续传。

上位机发送顺序示意（`command` 表示完成帧封装、发送和应答校验的函数）：

```python
source = generated_python.encode("utf-8")
assert 1 <= len(source) <= 16384
assert command(1).decode("ascii").startswith("KLQ ROBOT FW ")
command(2, struct.pack("<II", len(source), zlib.crc32(source)))
for offset in range(0, len(source), 1024):
    chunk = source[offset:offset + 1024]
    received = command(3, struct.pack("<I", offset) + chunk)
    assert struct.unpack("<I", received)[0] == offset + len(chunk)
command(4)
# 等 INFO 的 UI=1 后，按需 command(5) 启动。
```

`END` 成功并不表示 Python 语法和设备能力已通过运行检查。`RUN` 后通过 `INFO` 读取 `PY_ERROR` 与 `PY_LINE`；不支持的电机/红外设备会报运行错误，不应把下载成功显示成设备动作成功。

`INFO` 中主要字段：`USER_STATE` 为 0 空、1 下载中、2 已就绪、3 运行中；`USER_VALID` 为 0/1；`USER_LENGTH` 与 `USER_RECEIVED` 为字节数；`UI` 为 0 启动、1 待机、2 下载、3 完成提示、4 运行图标、5 用户图案、6 错误提示；`PY_ERROR` 和 `PY_LINE` 表示解释器最近错误及源码行号。错误码含义见 [Python 积木接口](python_api_v0.1.md#5-运行错误)。`PK_*`、`USB_RESETS` 等是调试字段，不应成为上位机业务判断的必需项。

通用应答状态码：0 成功，1 未知命令，2 请求长度错误，3 帧 CRC 错误，4 当前状态不允许，5 长度/偏移越界，6 Flash 写擦失败（引导侧），7 镜像或 RAM 程序无效（包括整包 CRC 失败）。`END` 后紧接 `RUN` 若收到 4，等待 `UI=1` 再运行。

## 与用户程序下载不同的命令

`RESET(7)` 会使系统固件复位并停留在 USB 引导；仅用于系统固件升级。引导中的 `BEGIN/DATA/END/RUN` 针对内部 Flash 系统固件，流程见 [USB 引导说明](../README_BOOTLOADER.md)。`ECHO(6)` 是通信诊断；`DISPLAY(8)` 是开发调试命令，Python 积木显示应生成 `klq.display_*()`；`FINISH(10)` 是主机通知运行结束的调试入口，通常由机器人自行判定结束。

现有可运行的协议客户端是 [`tools/klq_usb.py`](../tools/klq_usb.py)。例如 `python tools/klq_usb.py program user.py` 只下载，追加 `--run` 会等待完成图标结束并启动；该工具同时给出帧封装、CRC、混合日志重同步和重试的参考实现。
