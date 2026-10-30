> 2026-10-30暂停检查点：板上APP0.14、BOOT0.5；APP0.15已构建未装，KLQ2手机可见、PC BLE连接及下载待继续。准确当前状态见[暂停交接](bluetooth_paused_20261030.md)。

# KLQ蓝牙传输接口 V0.1（APP0.15 / BOOT0.5）

ECB02C经UART5透传，配置为Peripheral、广播名KLQ2。GATT服务FFF0，主机→电脑/APP通知特征FFF1，电脑/APP→主机写特征FFF2；完整16位UUID使用标准Bluetooth基UUID。先订阅FFF1通知，再向FFF2写入。是否已实际安装相应固件、连接地址和验证范围，以当前handoff/验证记录为准。

## 帧与发送节奏

蓝牙不另造文件封装，沿用[KLQ1协议](host_usb_protocol_v0.1.md)：16B帧头、LE序号/命令/长度/CRC32，CRC覆盖头前12B和payload。通知和GATT写入边界不是KLQ1帧边界，双方必须流式拼帧；不能按一次notify就是一个完整应答。

默认每次写20B、间隔约20ms，确认GATT支持更大包后才扩大。每路只允许一个未完成RPC；推荐DATA每块128或256B，协议仍支持原1024B源码数据块（加4B offset，payload最大1028B）。单次RPC客户端等待至少10s，固件蓝牙发送整帧上限8s，接收帧超过2s无新字节则丢弃未完成帧。设备主循环不因这段等待忙等。

USB和蓝牙各自拥有固定组帧、回复、紧急ACK和最近请求缓存，不共享正在发送的buffer。两路单独的seq空间，允许USB观测与蓝牙下载同时进行；同一路自己不能让第二个请求越过尚未完成的第一个。相同seq/cmd/length/CRC重试只重发缓存回复，不重复已完成副作用。CRC是传输完整性校验，不是访问鉴权或固件签名。

## 先识别模式

INFO(0x0001)仍使用兼容前缀：

- `KLQ ROBOT FW 0.15`：BEGIN/DATA/END操作16KiB RAM Python源码。
- `KLQ USB BOOT 0.5`：BEGIN/DATA/END写系统应用槽0x08008000，USB字样是历史兼容标签，两种链路都可进入此模式。

新增`LINK=0/1`表示本次INFO来自USB/蓝牙，`BLE_READY`、`BLE_CONNECTED`、`BLE_STATUS=1`指示通道及0x56状态能力。实际名称、版本、MAC通过0x56读取，不用把目标广播名当已配置成功的证明。GPIO已连接而跳过AT时，READY1/CONFIGURED0、名称等查询字符串可为空；不会为读取信息而打断已连接透传。

## 下载用户程序

1. INFO确认是APP。
2. BEGIN(2)：`uint32 source_bytes, uint32 source_crc32`，字节数1..16384。
3. DATA(3)：`uint32 offset + bytes`，严格连续，核对应答received_length。
4. END(4)：校验完整长度和CRC。成功后保留RAM文本，完成图标结束、UI待机后可RUN(5)，或由用户短按PC4启动。
5. STOP(9)停止运行，完整源码保留。普通蓝牙/USB断开不停止自主运行，不清完整源码；复位/断电会清空。

有效BEGIN取得该路下载所有权；另一条链路的BEGIN/DATA/END或其它写控制返回ERR_STATE(4)，不会清数据或偷换来源。END完成/CRC失败退出下载、STOP、所有者链路断开释放所有权；提前END而数据尚未收满时，保留当前路继续补传的能力。非所有者断开不取消正在下载的另一条路。已完成源码没有永久链路所有者，可正常离线按键运行。

STOP是显式全局安全动作，可由任一路执行，取消当前用户运行/半包和电机诊断动作。BOOT中的STOP中止当前固件下载并留在引导；已经失效的旧镜像不会被假装恢复。STOP也取消尚未提交的延迟RESET/RUN动作，但不能撤回已经发生的复位或跳转。

## 通过蓝牙更新主控应用

需要实际安装支持蓝牙的BOOT0.5及以后；仅安装APP0.15不能让驻留USB-only BOOT0.3支持蓝牙。主引导安装和局部写入由受控SWD/原有恢复流程处理，普通APP不擦BOOT。

APP收到RESET(7)时先停机、等待ACK进入UART后请求引导复位；模块已有连接时BOOT保留透传，不发送AT或复位模块破坏连接。客户端必须重新INFO确认BOOT，再将链接于0x08008000的BIN按原BEGIN/DATA/END下载。BEGIN先使镜像元数据失效，END完整验证长度/CRC/向量后最后提交magic。失败、断连、CRC错误或掉电不启动混合镜像，重新BEGIN全量下载；仍保留USB救援通道。

END成功、INFO VALID1后发送BOOT RUN(5)。蓝牙TX DONE仅证明MCU UART最后一位已发给模块，并不证明central已经收到GATT通知。RESET/RUN应答若丢失，先重连/INFO判断当前模式，不能盲目跨模式重放RUN（APP中的RUN表示启动RAM程序）。

两路同时控制时，待提交RESET/BOOT RUN仍在回包期间，另一条路的写操作返回ERR_STATE。发送已经失败且不再占用TX时，新的合法写操作可以清除旧未提交决定/缓存，旧请求之后需重新验证当前状态，不能借旧RUN缓存跳入被替换的镜像。

## 蓝牙允许命令

蓝牙提供INFO/ECHO、BEGIN/DATA/END、RUN/STOP/RESET、APP显示/完成控制及只读0x50语音快照、0x55电机快照、0x56蓝牙状态。0x40..43外设升级诊断和语音工程控制继续仅USB，蓝牙收到返回ERR_COMMAND(1)；不让蓝牙会话误占USB工程会话。蓝牙不推送USB ASCII日志。

## 0x0056只读蓝牙状态 ABI1

payload为空，成功应答数据固定88B（不含标准4B status）：

| offset | 长度 | 内容 |
| --- | --- | --- |
| 0 | 1 | ABI=1 |
| 1..3 | 各1 | ready / connected / configured |
| 4..7 | 各1 | init_stage / error / step / attempts |
| 8/12/16/20/24/28/32 | 各4 | LEu32 rx_bytes / tx_bytes / uart_errors / rx_overflows / tx_timeouts / init_timeouts / config_writes |
| 36 | 21 | name，NUL结尾 |
| 57 | 16 | version，NUL结尾 |
| 73 | 13 | MAC，NUL结尾 |
| 86 | 2 | 保留0 |

stage：0等待，1发AT，2等响应，3配置核验完成，4已有连接跳过AT，5重试等待。config_writes是收到SET成功OK的次数，不是模块Flash实际写次数；丢ACK但设置已经生效时可少计。它不代表每次启动都重写持久配置。

## 回归入口

`python tools/test_dual_protocol.py`编译实际protocol.c：APP711断言（含原USB497）、BOOT466断言，14个删除关键保护的负对照均须失败。覆盖跨路拼帧/缓存、下载所有权/断连、STOP、FMC失败释放、提前END、元数据失效、完整CRC、RESET/RUN延迟提交互斥与reset/TX重排。真实USB驱动和ble_serial分别运行自己的原生测试；这些不能替代本机蓝牙连接、真正Flash断电和实物验证。
