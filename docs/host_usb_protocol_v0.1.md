# KLQ 上位机 USB 通信协议 V0.1

本文给上位机实现者使用，描述如何把积木生成的 Python 源码下载到机器人并控制运行。积木生成的函数名及参数见 [Python 积木接口](python_api_v0.1.md)。机器人内部的传感器轮询、串口事务和电机驱动不由上位机调用。

当前主机0.14，2026-10-30已通过主机USB安装，驻留BOOT仍为0.3；见[0.14优化记录](optimization_014.md)。正常使用通常不插USB；下载完成后的RAM用户程序由机器人独立执行。关闭串口、电脑停止读数或USB会话重置，不作为完整程序的停止命令。需停止时发送STOP或使用实体电源键；MCU复位/失电仍清RAM。

0.14保持原KLQ1帧格式和积木下载命令，改为异步应答、完整RAM跨CDC会话保留及半包取消。每次RUN默认功率2，设备保护仅针对程序实际调用的端口/内置设备及拥有的电机；无外设和同型多设备均允许，详见[Python接口](python_api_v0.1.md)。本轮实板验证包含软件CDC关闭/重开与RAM流程、Windows DTR0下INFO/1024B ECHO；用户另确认当前供电下物理拔USB显示12保持、短按STOP/RUN及重连正常。尚未做电气断电注入。

外接固件升级能力自0.9加入：INFO中的 `EXT_UPGRADE=1; EXT_PORTS=3` 表示支持 OPEN/XFER/RESULT/CLOSE `0x0040..0x0043`。这是电脑专用升级通道，使用 [USB外接升级协议](../sensor_boot/docs/usb_bridge_v1.md) 及 `*_update.klq`，不增加用户积木函数。升级OPEN会停止用户程序；升级期间拒绝Python BEGIN/DATA/END/RUN/DISPLAY/FINISH，允许STOP。实体短按也不能启动旧程序；长按1.5秒会先取消会话再关机。关闭/取消后旧RAM程序可保留，但必须手动重新运行。

历史0.10加入`EXT_MOTOR=1`，通过上述端口会话提供电脑电机诊断SET_PWM/GET_STATUS。当前Python普通电机积木已接通，不能再用该历史版本的未接通状态描述0.14。电机诊断期间，实体短按或USB STOP可取消会话，不重新启动旧RAM程序；停止后仍应按电机无SET 250 ms归零及滑行特性处理。命令和工具见[电机诊断](../sensor_boot/docs/usb_bridge_v1.md#电机诊断主机010)。

## 连接与运行环境

连接机器人自身的 USB 数据口，打开其 CDC 虚拟串口。开发板当前使用 `VID:PID=314B:0108`、115200 8N1；COM 号会变化，不应固定。一次只能有一个程序占用该串口，串口监视器与下载器不能同时打开。

打开后先发送 `INFO(1)`：返回文本以 `KLQ ROBOT FW ` 开头时，`BEGIN/DATA/END` 才表示下载 Python 到 16 KiB RAM；以 `KLQ USB BOOT ` 开头时，同样的命令会**擦写内部 Flash 中的系统固件**，此时不得发送用户程序。RAM 程序在 MCU 复位或断电后失效，不存入内部或外部 Flash。当前蓝牙下载协议尚未实现。

以下异步发送/控制线规则针对APP0.14及本轮回归的源码实现，不能视为驻留BOOT0.3也已更新。APP0.14在USB已配置后，二进制命令与应答不以DTR为前提；不发送CDC控制线请求的客户端也可使用KLQ1。ASCII调试日志要求DTR打开、控制台可用且日志槽空闲，否则不组织或排队日志；发送停滞时暂停日志。未连接USB或无人读数不应拖住本机程序。

字节流仍可能混入ASCII日志。接收端须搜索`KLQ1`帧头并按序号、命令、CRC识别应答，不能假定每个字节都是协议帧。

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

0.14使用固定缓冲和异步TX游标，整个应答发送任务超过250ms仍未完成则失败并回收，主循环继续处理按键、端口、VM和停止；这不是PC端总命令超时的承诺。APP0.14的RESET只在应答实际发送完成后提交复位；发送失败后同序号同帧重试可恢复待提交动作，不再只回复成功而遗失动作。源码BOOT侧RUN有同样修复，但驻留BOOT0.3未安装这项更新；升级器仍应核对重新枚举后的INFO，不能只凭ACK判定已跳转。上位机正常仍按一请求一应答发送；积压期间的STOP、语音ARM0/取消有独立紧急处理路径，不应依靠不断新发普通命令挤占发送。

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

在BEGIN与END之间，设备观察到USB会话重置或CDC DTR从已打开变为关闭时，取消半包RAM程序并回待机；重连后必须重新BEGIN并发送全部DATA，不能从旧偏移续传。新的BEGIN已经覆盖旧源码，取消半包不会恢复旧程序。

已经END成功的完整程序，在同样的会话变化下保持有效：RUNNING继续运行，READY保持可运行；STOP后也保留源码。USB物理拔线只有在MCU仍有供电且没有系统复位时才符合这一RAM保留前提。

电脑软件close不保证驱动一定发出0x22 SET_CONTROL_LINE_STATE，尤其客户端从未置DTR时。设备只能响应实际收到的控制线/USB事件；本轮实板脚本主动拉低DTR再close/reopen并核对半包取消，另有用户物理拔USB后完整程序保留的实物确认；两者分别记录，仍未抓取0x22总线请求。上位机应显式管理DTR，避免把“关了电脑句柄”当作已证实设备收到断连。

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

`INFO` 中主要字段：`USER_STATE` 为 0 空、1 下载中、2 已就绪、3 运行中；`USER_VALID` 为 0/1；`USER_LENGTH` 与 `USER_RECEIVED` 为字节数；`UI` 为 0 启动、1 待机、2 下载、3 完成提示、4 运行图标、5 用户图案、6 错误提示；`PY_ERROR` 和 `PY_LINE` 表示解释器最近错误及源码行号。错误码含义见 [Python 积木接口](python_api_v0.1.md#5-运行错误)。`PK_*`、`USB_RESETS`等是调试字段，不应成为上位机业务判断的必需项。

0.14的SYS_FAULT/SYS_PC为保留RAM故障记录，SYS_WDT表示本次APP是否使能独立看门狗；SYS_WDT=1不等于已验证所有Fault或实际挂死可恢复。驻留BOOT0.3会清复位标志，看门狗复位原因日志仍可能缺失。INFO中的系统镜像VALID读校验缓存；启动及系统固件END仍做完整CRC，系统固件BEGIN使该缓存失效，不是每次INFO重算整镜像。

通用应答状态码：0 成功，1 未知命令，2 请求长度错误，3 帧 CRC 错误，4 当前状态不允许，5 长度/偏移越界，6 Flash 写擦失败（引导侧），7 镜像或 RAM 程序无效（包括整包 CRC 失败）。`END` 后紧接 `RUN` 若收到 4，等待 `UI=1` 再运行。

## 与用户程序下载不同的命令

`RESET(7)` 会使系统固件复位并停留在 USB 引导；仅用于系统固件升级。引导中的 `BEGIN/DATA/END/RUN` 针对内部 Flash 系统固件，流程见 [USB 引导说明](../README_BOOTLOADER.md)。`ECHO(6)` 是通信诊断；`DISPLAY(8)` 是开发调试命令，Python 积木显示应生成 `klq.display_*()`；`FINISH(10)` 是主机通知运行结束的调试入口，通常由机器人自行判定结束。

现有可运行的协议客户端是 [`tools/klq_usb.py`](../tools/klq_usb.py)。例如 `python tools/klq_usb.py program user.py` 只下载，追加 `--run` 会等待完成图标结束并启动；该工具同时给出帧封装、CRC、混合日志重同步和重试的参考实现。


自0.13保留可选只读0x55普通电机快照，能力EXT_MOTOR_STATUS=1，详细字节布局见[电机观测](host_motor_status_v0.1.md)。用户Python下载与运行协议不变。
