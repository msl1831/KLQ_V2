# 主控 USB 到外接端口升级协议 v1

2026-10-29。PC 通过科乐机 APM32E103RET6 的 USB CDC 连接主控，再由主控转发到外接端口 1、2、3。传感器和电机使用 [既有 UART 引导协议](protocol_v1.md) 与 `*_update.klq` 包。当前已合入 `codex/python-runtime` 的0.9业务固件，保留Python、26积木、PC4及显示/采样调度。旧main的0.3镜像是历史软件验证基线，不能代替本版。

主控 USB 应用 INFO 以 `KLQ ROBOT FW 0.9` 开头，声明 `EXT_UPGRADE=1; EXT_PORTS=3`，同时保留PY_ERROR、PY_LINE和PC4状态。主控常驻 USB 引导不提供外接端口升级；PC 需先运行带此能力的系统固件。外接板需已安装对应 sensor_boot 工厂镜像，旧 UL/IR 独立应用无法仅通过 UART 增加常驻引导。主控旧USB引导0.3可以安装0.9应用，无需为本功能更新主控引导。

同日IR实板验证发现旧板端引导Flash完成标志缺陷；当前必须使用板端引导1.0.2及更新后的PC工具。工具在BEGIN前核对引导版本，拒绝1.0.0/1.0.1/未知版本。ENTER期间的typed UART错误只允许通过重新确认同UID引导INFO恢复，再继续布局校验，不会跳过写入门禁。IR两次完整下载/END/RUN及30秒采样恢复已通过，见 [IR实测](../../docs/verification_20261029_ir_via_host.md)。原0.9合集中的旧factory及旧工具不包含本次修复，不能继续用于新安装/升级。

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

XFER允许 INFO、HOLD、ENTER_BOOTLOADER、STOP、电机/传感器BEGIN/DATA/END/RUN和GET_LAYOUT；0.10追加下述电机诊断SET_PWM/GET_STATUS。每次最多一个事务；相同内层帧的重发复用结果，不重复执行。终态后新内层序号可提交下一个事务；同序号改内容会拒绝。PC在外层ACK丢失时重发完全相同的USB请求；确认UART超时后才使用新内层序号重试同一逻辑请求。

## 主控转发约束

主控使用固定缓冲和非阻塞 UART 状态机，仅缓存一个分包和响应，不缓存整包固件。OPEN先取消目标端口旧请求并排空短期残留；其它两个端口继续轮询。目标端口旧样本清空，避免升级期间暴露过期在线数据。

首次有效 INFO 锁定设备 UID，后续 INFO 必须保持同 UID、板型、兼容号、类别和类型。进入引导后重新读 INFO 并核对 GET_LAYOUT，才能转发写入请求。BEGIN的目标与长度、DATA的页边界和最后一页长度由主控再次检查；板端仍独立检查应用内部身份和 CRC。

电机在应用模式收到进入引导请求时，主控先发送 STOP 并等待成功，再发送 ENTER。主控在设备复位后自动发送并重试 HOLD，确认保持后才完成电脑的 ENTER 事务；即使 ENTER 应答丢失也尝试 HOLD。初次 INFO发现设备已在引导时，同样先自动 HOLD 再向电脑交付 INFO。这段握手不等待电脑下一轮 USB 请求。

USB响应等待期间仍推进PC4电源键和外接端口状态机。会话活动时暂停非请求USB文本日志；USB解析每轮最多读取256 B，防止持续输入饿死UART。USB复位/重新配置清理解析和会话状态；跨USB配置变化的旧发送不会报告成功。会话活动期间拒绝用户程序BEGIN/DATA/END/RUN/DISPLAY/FINISH，实体短按也不能启动旧程序；仍允许STOP和显式主控RESET，长按1.5秒先取消会话再关机。RESET先停止用户程序并取消会话。USB仅断开重连时完整RAM程序仍保留，半包RAM下载取消。

## 验证入口

```powershell
& .venv/Scripts/python.exe tools/build.py --demo --output-dir build/usb_sensor_bridge/app
& .venv/Scripts/python.exe tools/build.py --output-dir build/usb_sensor_bridge/boot
& sensor_boot/tests/run_host_bridge.ps1
& sensor_boot/tests/usb_host/run_tests.ps1
& sensor_boot/tests/run_pipeline_tests.ps1
& .venv/Scripts/python.exe -m unittest discover -s sensor_boot/tests -p test_host_tool.py -v
```

业务合流的历史资源及旧包哈希见 [主控0.9验证](../../docs/verification_20261029_host_upgrade.md)。[来源验证记录](verification_20261029_usb_bridge.md)保留旧main软件基线。当前1.0.2已包含RUN应答/TC失败保持引导的修正和对应native边界回归；IR的真实Flash/USB升级正常流程已通过，真实发送故障注入、其它板及断电中断仍需单独验收。


## 电机诊断（主机0.10）

当前主机INFO为`KLQ ROBOT FW 0.10`，额外声明`EXT_MOTOR=1`。在原OPEN/XFER/RESULT/CLOSE会话内，先INFO锁定board2/hw2、UID及APP模式，新增：

| 内层命令 | 请求 | 成功响应数据 |
| --- | --- | --- |
| SET_PWM `0x0300` | pwm:i16，±1000 | 相同pwm:i16 |
| GET_STATUS `0x0301` | 空 | uptime:u32,pwm:i16,reserved:u8=0,active:u8 |

检查长度/范围、回显和状态字段。SET等待UART ACK100 ms且不重发；PC也不得因超时重新发送旧转速，完全相同帧仍复用缓存结果。只有新SET续期，INFO/STATUS不续期。STOP `0x0302`仍可使用。

PC4短按、USB STOP、USB重连会取消电机诊断；原固件升级会话的短按行为保持。取消停止后续SET，依赖接收端250 ms无SET归零兜底；未独立实测故障取消时延。正常工具结束先STOP、STATUS0、CLOSE，不把CLOSE响应当作输出归零确认。

暂停观测工具、核对端口/UID且电机悬空后使用（COM/端口按实际接线）：

```powershell
python tools/test_motor_live.py --port COM123 --sensor-port 3 --uid 4150354C/38323609/00D86C62 --pwm 200 --seconds 1 --report build/motor_live.json
```

默认限±300，显式`--allow-high-pwm`才允许±800，单次≤2秒。N20本轮20%未转、用户明确要求80%后转动并停止，见[实物记录](../../docs/verification_20261029_motor.md)。该诊断不写Flash，也不代表Python电机驱动已完成；不要用旧factory重装当前板卡。
