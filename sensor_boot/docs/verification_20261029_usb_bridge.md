# USB 外接端口升级转发验证 2026-10-29

本轮完成电脑→APM32主控USB→外接UART→UL/IR/PY32电机的升级软件链路。主控应用标记0.3，USB INFO新增EXT_UPGRADE/EXT_PORTS。工作基线为 `main@63d1ea38f69fbd478f92751855f238340b6fa853` 加当前未提交工作区；没有切分支、合并python-runtime、提交或推送。原有语音、IR、UL、电机和其它未提交工作保留。

未访问真实COM口、探针或板卡，未烧录。以下证据属于构建、静态镜像检查和原生软件模拟。

## 实施与修复

- 主控新增OPEN/XFER/RESULT/CLOSE会话，随机token、单端口独占、原请求幂等、固定缓冲、每轮一个UART发送字节和有限重试。其它端口保持普通轮询；关闭、USB复位或30秒闲置后清旧样本并重新发现。
- INFO固定板型/兼容号/类别/类型/UID；ENTER或RUN后清除引导身份门禁，必须重新读到同UID的引导INFO并验证GET_LAYOUT后才能擦写。身份不符/INFO失败不能通过单独重读LAYOUT重新打开写入门禁。DATA丢ACK仍可保留已验证身份及布局，用新序号重试。
- 电机应用先STOP且收到成功后才ENTER。主控自动执行复位后HOLD，ENTER丢ACK也继续HOLD；首次发现引导INFO时同样先HOLD。电脑不参与这段500 ms时序握手。
- 升级期间停止用户程序并拒绝新的用户下载/RUN/DISPLAY/FINISH；允许STOP。暂停非请求USB日志。USB解析每轮256 B，USB发送等待期间继续推进端口状态机，避免阻塞引导握手。
- 复现并修复USB响应中途复位的会话泄漏：接收循环每次读取前检查reset_seen并取消旧会话；USB读函数在未消费复位事件时暂停交付字节。发送引入reset/configuration/overflow的epoch检查，跨代发送失败时不改写新配置端点。
- 电脑工具保留直连UART，增加 `--via-host --sensor-port 1..3`。本地检查包后才打开串口，核对主控能力、OPEN、提交帧、查询结果及CLOSE；异常/KeyboardInterrupt尝试释放。外层ACK丢失保留原请求；确认UART超时后才改内层序号重试。BEGIN考虑主控3次10秒UART预算，电脑结果期限为32秒。

## 构建与资源

编译器为既有ARMCC5.06 update6 build750，Python3.12.14，没有新增固件库依赖。修改前将bootloader源码复制到 `build/sensor_bridge_baseline`，排除本轮改动后独立重建基线；最终构建到 `build/usb_sensor_bridge`。

```powershell
& .venv/Scripts/python.exe tools/build.py --demo --output-dir build/usb_sensor_bridge/app
& .venv/Scripts/python.exe tools/build.py --output-dir build/usb_sensor_bridge/boot
```

| 镜像 | ROM/BIN B | RO B | RAM B | 相比本轮基线 ROM/RAM |
| --- | ---: | ---: | ---: | --- |
| 修改前主控应用 | 16508 | 16284 | 26432 | 基线 |
| 修改后主控应用0.3 | 19760 | 19528 | 26720 | +3252 / +288 |
| 修改前主控引导 | 9272 | 9084 | 8728 | 基线 |
| 修改后主控引导 | 9388 | 9196 | 8728 | +116 / 0 |

主控应用链接上限489472 B，引导32768 B，RAM链接上限131064 B，均未越界。主控保留原有2048 B栈及512 B堆预留；新增桥接使用静态存储，不调用堆分配。三类PY32镜像及128 B分页协议本轮未改，沿用此前sensor_boot产物。

## 镜像与可转交包

| 文件 | 目标/物理地址 | SHA-256 |
| --- | --- | --- |
| `build/usb_sensor_bridge/app/klq_demo_app.bin` | APM32E103RET6，`0x08008000..0x0800CD2F` | `da1c5428fde35a4ffdf9578d05a8504e6b6ac76caa76bd510df4f46a60b1d1d7` |
| `build/usb_sensor_bridge/boot/klq_bootloader.bin` | APM32E103RET6，`0x08000000..0x080024AB` | `2778253c659facc74eb16c842b2adb8525de09060a58ee57451f0c23d0fc7546` |
| `build/usb_sensor_bridge/release/klq_usb_sensor_bridge_0.3.zip` | 61169 B，主控应用/电脑工具/三类升级包 | `a773168b848d9e8bee6e633faba794732d8089a482ad592374a9dbaf43204a3b` |

独立核对两份HEX每条记录校验和、地址范围、重复地址、与BIN逐字节一致性、向量和有效数据长度；应用19760 B、引导9388 B。应用没有写到引导或末尾主控有效记录区。完整镜像/源文件哈希、map读数及ZIP校验固化在 [usb_bridge_build_evidence_20261029.json](usb_bridge_build_evidence_20261029.json)。

ZIP中包含主控应用BIN/HEX、update/host_link/pack/klq_usb电脑脚本、pyserial3.5清单、三种 `*_update.klq`、README和manifest。主控固件对应当前main功能基线，未合入另一个分支的解释器等功能。外接板首次安装仍须对应sensor_boot工厂镜像，不能把应用升级容器当SWD工厂镜像。所有本轮产物位于忽略的build目录；正式跨电脑交接应另传ZIP或在固定输入下重建，并完成源码提交/远端HEAD核对。

## 软件测试

```powershell
& sensor_boot/tests/run_host_bridge.ps1
& sensor_boot/tests/usb_host/run_tests.ps1
& sensor_boot/tests/run_pipeline_tests.ps1
& .venv/Scripts/python.exe -m unittest discover -s sensor_boot/tests -p 'test_*.py' -v
& IR/tests/run_host_tests.ps1
& .venv/Scripts/python.exe -m unittest discover -s tools -p test_protocol.py -v
```

- 实际external_ports.c桥接内核：161次断言通过，覆盖排空、非阻塞TX、其它端口轮询、租约/时钟回绕、状态/页门禁、UID变化、ENTER后的重新身份检查、STOP/ENTER/HOLD、重放、超时及释放/恢复。
- 实际protocol.c USB层：300次断言通过。桥接API为mock，验证长度/CRC/token/范围错误无下游动作、用户命令独占门禁、相同序号RESULT能推进、重复OPEN/XFER交给核心幂等处理、RESET/USB reset、响应期间复位回归、256 B接收预算与能力声明。
- Python完整测试30/30，无跳过：原13项包/直连工具、新13项USB工具、4项实际C链路测试。USB工具覆盖全部板型/端口、错误包先于串口访问、能力和token检查、外层ACK丢失、终态UART超时重试、异常/中断释放、噪声与错CRC/序号、直连路径保留。
- 完整链路使用真实Python HostLink/upgrade→实际C USB解析→实际C端口转发→实际C PY32引导解析/更新核心。三种本轮完整应用分别通过端口1/2/3下载并END提交；另测错板在BEGIN前拒绝、首个页后USB复位重新开会话续传且不重发页0、UART DATA应答全部丢失后的清理和重连续传。
- 完整C链路保持引导至END，原生进程不执行ARM应用；ENTER/RUN及丢ACK状态由独立C核心和电脑工具测试覆盖。USB硬件发送等待中的epoch和服务推进代码完成固件编译与复核，原生USB协议层使用mock USB接口。
- IR原有采样1190、协议107、主控端口101次断言回归通过；主控USB格式单测2/2通过。VC98仍报告原有窄化转换警告，新增桥接核心转换警告已消除；ARMCC最终构建无编译错误。

## 实板接续

先核对主控当前安装版本/功能分支和真实USB COM，确定要安装的主控应用；再分别核实外接板型号、UID、供电及初装引导状态。执行真实USB枚举、三个端口/三类设备更新、默认RUN后的应用INFO与业务回归。特别验证电机STOP及250 ms失联、IR关发射、USB拔插/主控复位/外接板掉电、BEGIN擦除/中间DATA/END提交断电、长BEGIN时USB延迟和自动HOLD的最坏时序。软件模拟结果不代表上述硬件验收已完成。
