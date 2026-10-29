# 主控 USB 到外接端口升级协议 v1

2026-10-29。PC 通过科乐机 APM32E103RET6 的 USB CDC 连接主控，再由主控转发到外接端口 1、2、3。传感器和电机使用 [既有 UART 引导协议](protocol_v1.md) 与 `*_update.klq` 包。当前主控工作基线是 main 工作区，保留此前语音、IR 和其它未提交改动，没有合并 python-runtime 分支。

主控 USB 应用 INFO 以 `KLQ ROBOT FW 0.3` 开头，增加 `EXT_UPGRADE=1; EXT_PORTS=3`。主控常驻 USB 引导不提供外接端口升级；PC 需先运行带此能力的系统固件。外接板需已安装对应 sensor_boot 工厂镜像，旧 UL/IR 独立应用无法仅通过 UART 增加常驻引导。

## 电脑操作

在仓库根目录执行；COM10 为主控的 USB 串口示例，端口号对应实际接线：

```powershell
# 超声波，接主控外接端口1
& .venv/Scripts/python.exe sensor_boot/tools/update.py --via-host --port COM10 --sensor-port 1 --image sensor_boot/build/ul/release/ul_update.klq

# 红外，接外接端口2
& .venv/Scripts/python.exe sensor_boot/tools/update.py --via-host --port COM10 --sensor-port 2 --image sensor_boot/build/ir/release/ir_update.klq

# 电机，接外接端口3
& .venv/Scripts/python.exe sensor_boot/tools/update.py --via-host --port COM10 --sensor-port 3 --image sensor_boot/build/motor/release/motor_update.klq
```

每种设备均可接端口1～3，工具按升级包身份核对所选端口的实际设备。包的长度、向量、内部板型及 CRC 在打开串口前校验。`--stay-in-boot` 可在提交后保留引导；默认 RUN 后查询同 UID 的应用 INFO。原先省略 `--via-host` 的 PC 直连 UART 用法继续可用。

同一 USB COM 口同时由一个工具占用。升级会停止当前用户程序，并暂停目标端口的普通轮询；完成、失败或取消后不会自动恢复旧程序或电机动作。关闭工具的正常异常路径会尝试 CLOSE。USB 断开/重新配置或长时间没有会话请求时，主控释放目标端口、清除旧样本并重新发现；设备若停在引导，普通发现不会替它运行未完成的应用。

## USB 外层帧

沿用主控 KLQ1：`magic[4], seq:u32, cmd:u16, payload_length:u16, crc32:u32, payload`，小端。CRC32 覆盖头前12 B及payload，响应cmd OR `0x8000`，响应payload前4 B为主控状态。原有 USB 命令与传感器 UART 命令在不同层处理，不直接混用其命令号。

| 命令 | 编号 | 请求payload | 成功响应数据（去除主控status） |
| --- | --- | --- | --- |
| OPEN | `0x0040` | port0:u8, token:u32, board:u16, hw:u16，共9 B | token:u32 |
| XFER | `0x0041` | token:u32 + 一帧完整 UART 请求，共20～152 B | 空，表示已受理 |
| RESULT | `0x0042` | token:u32 | state:u8 + 对应数据 |
| CLOSE | `0x0043` | token:u32 | 空 |

port0为0～2，用户工具显示1～3。token由PC随机生成，必须非零且每次新会话重新生成。OPEN锁定端口、板型/兼容号；重复同一活动会话OPEN幂等，其它token不能抢占或关闭它。成功关闭后的token不可再次OPEN；同token的CLOSE可重试，避免丢ACK导致假失败。

主控状态为0成功、2长度错误、3CRC错误、4无会话/忙/顺序错误、5端口/板型/范围或非白名单命令错误。UART设备状态保留在内层响应，不能把外层“受理成功”当成设备已写入成功。

RESULT的state=0表示处理中或尚无事务；state=1后跟完整UART响应帧（含设备status）；state=2后跟reason:u8，1超时、2UART错误、3设备身份不匹配、4非法响应/分区、5取消。结果保留至下一个不同事务。查询不复用旧USB缓存，重复相同序号的查询也能观察状态推进。

XFER只允许 INFO、HOLD、ENTER_BOOTLOADER、STOP、电机/传感器BEGIN/DATA/END/RUN和GET_LAYOUT。每次最多一个事务；相同内层帧的重发复用结果，不重复执行。终态后新内层序号可提交下一个事务；同序号改内容会拒绝。PC在外层ACK丢失时重发完全相同的USB请求；确认UART超时后才使用新内层序号重试同一逻辑请求。

## 主控转发约束

主控使用固定缓冲和非阻塞 UART 状态机，仅缓存一个分包和响应，不缓存整包固件。OPEN先取消目标端口旧请求并排空短期残留；其它两个端口继续轮询。目标端口旧样本清空，避免升级期间暴露过期在线数据。

首次有效 INFO 锁定设备 UID，后续 INFO 必须保持同 UID、板型、兼容号、类别和类型。进入引导后重新读 INFO 并核对 GET_LAYOUT，才能转发写入请求。BEGIN的目标与长度、DATA的页边界和最后一页长度由主控再次检查；板端仍独立检查应用内部身份和 CRC。

电机在应用模式收到进入引导请求时，主控先发送 STOP 并等待成功，再发送 ENTER。主控在设备复位后自动发送并重试 HOLD，确认保持后才完成电脑的 ENTER 事务；即使 ENTER 应答丢失也尝试 HOLD。初次 INFO发现设备已在引导时，同样先自动 HOLD 再向电脑交付 INFO。这段握手不等待电脑下一轮 USB 请求。

USB响应等待期间仍推进外接端口状态机。会话活动时暂停SC7/端口/语音的非请求USB文本日志；USB解析每轮最多读取256 B，防止持续输入饿死UART。USB复位/重新配置清理解析和会话状态；跨USB配置变化的旧发送不会报告成功。会话活动期间拒绝用户程序BEGIN/DATA/END/RUN/DISPLAY/FINISH，仍允许STOP和显式主控RESET；RESET先取消会话。

## 验证入口

```powershell
& .venv/Scripts/python.exe tools/build.py --demo --output-dir build/usb_sensor_bridge/app
& .venv/Scripts/python.exe tools/build.py --output-dir build/usb_sensor_bridge/boot
& sensor_boot/tests/run_host_bridge.ps1
& sensor_boot/tests/usb_host/run_tests.ps1
& sensor_boot/tests/run_pipeline_tests.ps1
& .venv/Scripts/python.exe -m unittest discover -s sensor_boot/tests -p test_host_tool.py -v
```

完整软件证据、资源增量和交付包哈希见 [本轮验证记录](verification_20261029_usb_bridge.md)。原生模拟覆盖实际USB解析、实际端口桥接和实际板端C更新内核；UART寄存器、USB硬件、Flash驱动及ARM应用跳转仍需实板验收。
