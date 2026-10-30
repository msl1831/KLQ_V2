# KLQ 主控当前交接（2026-10-30）

## 最新：只读架构检查（2026-10-30）

用户要求整体检查架构及优化。本轮代码基线8284847；未访问设备/串口、未烧录、未下载RAM、未触发运动，生产源码保持原样。详见architecture_review_20261030.md/json及更新后的architecture_current.md。确认待修：首次RESET应答失败后同序号重试只ACK不执行动作；重复或积压旧样本被当新鲜。另需优先处理USB同步等待、传感器故障等待/电机续行策略、驱动超时先于VM及Fault/watchdog恢复；策略去重、INFO全镜像CRC、关机额外300ms、跨程序功率默认、显示去重等已列优先级。**这些审查发现尚未实施，不能认为0.13已经修复。**

真实C最小复现分别在build/architecture_usb_power_20261030、architecture_python_review_20261030、architecture_review_ports_20261030，调用图在build/architecture_review_20261030/audit.htm。静态路径660B+Unknown，不当作完整栈证明。0.13最近安装事实与RAM示例看下节，当前设备状态本轮未重查。共享F/main仍不覆盖；后续先修第一批，再做相应板测，不自动修改部署。


## Python电机积木已完成本轮（2026-10-30，最新）

用户明确电机一直悬空，后续不再重复机械确认。当前主机0.13已USB安装，APP30952/RO30884/RAM27632 B（比0.12 Flash+248、RAM不变），SHA6637592656fdc0ebc5b681cb569433e5f1c40f9317c038ee342db9edd6d690ae。P2 N20实际motor_for正反和motor_run/motor_stop完整用户确认，另stop积木/EOF/参数错误/USBSTOP四例普通ACK停机通过，用户看到短转停止但未核对次数。异步故障→PY7只native验证。0x55只读快照和页面已启用，默认档位2恢复；当前RAM保留examples/motor_blocks_demo.py，READY/有效、未自动RUN、goal/ACK0、语音ARM0。驻留boot0.3/外设未写。详细见verification_20261030_python_motor.md/json及python_motor_worklog_20261030.md；旧60s工具窗口超时不当作电机故障，人工复测用RAM保留方式。当前提交/远端及PID收尾核对。


## 历史：0.12语音与USB复测（2026-10-30）

用户已恢复测试并确认N20仍悬空。0.11新增下载中/诊断占用真实语音拒绝、READY语音1s动作、活动中USB STOP、活动中PC4短按五项均软件证据+用户确认PASS，PC4不误启动READY程序。每项ARM0/清RAM/真实STOP ACK。

随后修复USB EP读改写的RC_W0标志保留遗漏（11处），纯模型及负对照通过、独立审查通过；不能称此前Write timeout已定因。0.12已通过主机USB安装，APP30704/RO30636/RAM27632 B，比0.11仅多40 B Flash，SHA d3242f3958ae24eb4b240c5b9b8be796e63cf47f47a0045aa06ccbf3301f3335；驻留引导仍0.3且未应用该修复，外设固件未写。20次串口开关+240边界ECHO+5次观测交接通过，无额外USBreset，CI版本和RAM无延时循环RUN/STOP复用回归通过。已恢复正常监测，ARM0/空RAM。 观测页假在线已修，8组mock通过；确认released后安全重载为PID20812（下次重查），实机新INFO/voice/三端口快照通过。P1 IR/P2 N20/P3 UL。

详见[本轮验收](verification_20261030_voice.md)、verification_20261030_voice.json和[语音工程USB接口](host_voice_protocol_v0.1.md)。代码仍以C工作树codex/python-runtime为准，提交/远程状态结束时核对；F共享main不覆盖。剩余Python电机/移动实物、USB物理拔线、关机复位150ms实际时序及长期USB稳定性，不把模型或短测当作这些验收。以下暂停记录保留为历史。


## 历史：用户暂停检查点（2026-10-30）

用户要求暂停其他测试。主机0.11已安装，UART4真实CI0.2.0版本、8音效听音、真实语义及EMPTY有限正反转/停止通过；RUNNING时语音正转/停止拒绝、不结束用户程序，用户确认电机未转。收尾已ARM0/active0/空RAM/精确P2 UID后的真实STOP ACK。P1 IR/P2 N20/P3 UL。监测恢复后又出现USB Write timeout，无INFO快照；现HTTP pause200/releasedtrue，保留页面但停止重试，不宣称正常在线。根因留待恢复后查。

代码在codex/python-runtime@64837d5之上未提交/未推送；F共享main包含其他任务，勿覆盖。详细见[暂停交接](voice_integration_paused_20261030.md)和[工作记录](voice_integration_worklog_20261030.md)。READY/DOWNLOADING/排他/活动取消/关机复位新时序实物待测；不自动ARM。以下为此前进度。


当前主控分支为 `codex/python-runtime`，本机工作树 `C:/Users/Administrator/.codex/worktrees/python-runtime/Codex`。合流源码ce714f7已推送。2026-10-29同日后续用户授权通过主机自身USB安装：实际COM123已从0.8升级到0.9并回到待机；原引导0.3保留，0.4只做过构建回归。实际应答/CRC和范围见verification_20261029_usb_install.md及usb_install_20261029.json。接力仍先查HEAD/工作区，并重新识别端口，不能照搬COM号。

共享 `F:/work/KLQ_V2/Codex` 仍为旧main@63d1ea3加未提交工作。按sensor boot对话转达的用户要求，将其USB→UART升级代码真实合流到本分支，未整文件覆盖Python业务；未改写共享源码。sensor_boot及UL/IR/MOTOR第一方输入已纳入本分支，44个板端生产文件与当前来源逐字节一致。语音、环境审计工具及其它共享未提交材料仍未纳入，不能宣称克隆即可恢复整个产品。Puya SDK以sensor_boot/dependencies.json固定提交，恢复见development_setup.md，不跟踪裸gitlink。

## 历史工作记录

- 2026-10-30继续测试：右侧监测已恢复，主机0.10/P1 IR/P2 UL/P3 N20在线。用户确认悬空后，-800/1秒反转及单次+800不续期自动归零均通过，用户确认两次方向相反且均停止；迟到STATUS在设备uptime相隔383 ms时为0/0，不代表精确250 ms边界。之后约9.77秒三口无新增通信错误。目标固件未改/未烧录；仅修正页面电机0mm假测距显示、通用超声名称。另据共享main既往烧录/读回记录，Motor最新已知引导为1.0.2（本轮未查引导）。见verification_20261030_continue.md、continuation_20261030.json，原始文件及当前PID在build/continuation_20261030。

- 同日后续电机实物通过：主机经USB从0.9到0.10应用，24404 B（Flash+228/RAM仍26784 B），新增EXT_MOTOR及选定口0x0300/0301诊断，SET超时不重发，PC4/USBSTOP取消控制。P1 IR/P2 UL/P3 N20；motor UID4150354C/38323609/00D86C62/APP2.0.0。20%/2秒未转；用户要求80%后，+800/1秒实物转动并停止，STOP/STATUS0/CLOSE通过。电机固件未改，Python电机接口仍未接通；启动阈值和取消时延待测。观测PID31352（重查），三口在线。原生303/239/322、Python41、工具12、26积木164175断言通过。见verification_20261029_motor.md、motor_live_20261029.json；日志/0.9恢复镜像在build/motor_live_20261029。

- 同日后续UL经主控USB正常升级流程通过：当前接线P1 IR/P2 UL/P3空；UID4150354C/3832360B/0064B862，实际APP1.0.2→boot1.0.2→APP1.0.2。用共享main尚未提交的最新RCWL9610A修复包完整重写5832 B/46包，3.328秒，END/CRC/RUN/同UID及主机ECHO通过；不使用本分支旧UL5776 B应用。本轮无SWD、无固件源码变更。之后30秒两口有效采样，无新增TO/FE/UE或序号重头，UL报告23 mm（未独立校准）。观测PID27832（接力重查）。见verification_20261029_ul_via_host.md、ul_via_host_20261029.json；冻结包/源快照及原始日志在build/ul_via_host_20261029。

- 同日后续IR经主控USB升级通过，最新板上IR引导1.0.2、APP仍1.0.0（同包重写验证）。曾发现ENTER UART错误容错缺口、旧引导EOPIE缺陷；修复后从引导态及应用态各完整传6052 B/48包、END/CRC/RUN/同UID确认通过。用户明确授权仅SWD更新IR引导；128 B页RAM写入器备份/读回确认APP/metadata/选项未改，APP随后只经USB写入。最终监测PID28828（接力重查），P1/P2有效采样30秒无新增通信错误，主控仍0.9、UI1/PY_ERROR0。见verification_20261029_ir_via_host.md和ir_via_host_20261029.json。

- 同日传感器实测：观测页http://127.0.0.1:8765已恢复，PID30228（接力重查），P1超声/P2双路IR在线，主机接电池、传感器由端口供电。超声移动目标与IR左右独立响应通过；P2在14:48:56非人为掉线、约1秒后同UID恢复并序号重头，TO+3/FE+1，原因未定。恢复后45秒两口无新增通信错误，不能据此关闭间歇故障。见verification_20261029_sensors_live.md及sensor_live_20261029.json。当前0.9桥接不支持IR GET_STATUS，尚未拿到reset_flags/uptime。

- 同日实板新增证据：0.3引导收到24176 B应用，END CRC通过、VALID=1；RUN后INFO为0.9、USER_STATE=0、USER_VALID=0、UI=1、PY_ERROR=0、EXT_UPGRADE=1、EXT_PORTS=3，ECHO通过。仅安装主控应用，没有访问DAPLink或执行外接固件升级；安装后没有下载用户Python程序。

- 用户程序只在 16 KiB RAM，完成/停止/错误保留文本，新的 BEGIN 覆盖；复位/断电清空。无延时循环由协作执行保证可停止。
- 26 个非语音映射见 `docs/python_api_v0.1.md`，下载协议见 `docs/host_usb_protocol_v0.1.md`；内部调度/驱动不是上位机接口。
- 固件 0.8 修正 CS100A 有效样本状态 1 的距离等待；此前状态 0 的判断已复现并修正。
- 0.9保留16 KiB RAM、全部26项积木、PC4/双眼、SC7 50 Hz及CS100A运行25/待机10 Hz、80 ms快照。新增IR端口20 ms解析和升级桥接，Python电机控制/IR等待条件仍未实现；设备协议现已存在，不能继续标“待提供协议”。
- USB新增0x40～0x43和EXT能力，端口独占停止旧程序、PC4不能重启；关闭、USB epoch变化、30秒租约到期重新发现但不恢复动作。长按1.5秒先取消会话再关机。完整RAM仅USB重连保留、半包下载取消。
- 26积木在本次工作树重新通过22组/164175断言；原解释器/runtime源码未改变。新增真实7模块C共存测试176断言；端口桥接161、USB300；Python30/30无跳过，其中4个完整链路覆盖三类×三口共9次整镜像传输。IR1190/107/104、UL pending reset、Motor安全/更新/应用及工具清理7例均通过。详见verification_20261029_host_upgrade.md和host_upgrade_build_evidence_20261029.json，最新积木报告为blocks_sim_20261029.json。
- 应用BIN24176/RO23884/RAM26784 B，比独立重建ff33f97增3764/336 B。引导回归BIN9944/RO9740/RAM8744 B，增100/8 B。主控和六个板端镜像全通过地址、向量、HEX/BIN/CRC/manifest核验，新增桥接无堆分配。
- 最新ZIP为build/host_bridge_0_9/release/klq_host_0.9_peripheral_updates.zip，129839 B/23项，SHA-256 a3a6e9d7a9fb4bb6d9334b050da3c3a221b2ab2949081be6b0190dafa5422237。含当前0.9应用及三类最新工厂/升级容器，已排除旧Motor2856 B副本。主控现有引导0.3兼容0.9应用，无需为桥接更新引导。build/outputs不入Git，交接须另传ZIP或按脚本重建。
- 历史10月14日实板41项接口通过、COM123/P2独立供电/SC7在线、结束RAM清空及观测服务恢复，证据保留于verification_20261014_blocks.md。10月29日合流阶段只做软件验证，后续USB安装已另外记录；不能把旧传感器接线/供电/服务状态当成此次实查。

## 复查命令

```powershell
python tools/test_blocks_sim.py --report build/blocks_sim.json
& sensor_boot/tests/run_business_tests.ps1
& sensor_boot/tests/run_host_bridge.ps1
& sensor_boot/tests/usb_host/run_tests.ps1
& sensor_boot/tests/run_pipeline_tests.ps1
python -m unittest discover -s sensor_boot/tests -p 'test_*.py' -v
python tools/package_host_release.py
```

其余构建、SDK恢复和板端回归命令见development_setup.md。下列为另行安排实板时的入口，不能视作本轮已执行：

```powershell
python tools/test_blocks_live.py --port <机器人CDC端口> --report build/blocks_live.json
python tools/status_dashboard.py
```

本机模拟采用已安装 VC98；GCC/Clang 需含汇编器与链接器，缺失时可用 `--cc` 显式指定完整工具链。模拟无串口操作；实板测试覆盖当前 RAM 程序，结束时软件复位清空 RAM。先停止监测服务再运行串口测试；自动测试期间不移动板子、不操作按键。

退出流程曾在 Windows CDC 重连时卡住。现已先保存功能报告，再用限时子进程发送 RESET/RUN；最终退出函数另行验证通过，主机和页面已恢复。保留功能测试与退出验证的证据边界，勿将电脑工具等待误称为固件用户循环卡死。

## 历史待办（以顶部最新记录为准）

1. 实物确认 PC3 按下/释放极性、SC7 安装前后左右、10 种表情与数字图案；本轮模拟和当前输入检查不替代这些验收。
2. 按已归档电机协议落实主控驱动、周期控制/保活，再测250 ms子板失联停机、motor_for、持续动作、双端口回滚及实际停车。0.10诊断已有N20正值80%/1秒转动和停止实物证据；Python动作仍仅mock。
3. 双路IR普通采样已合入；Python wait_ir条件/通道规则尚待实现，目前仍错误6。语音暂不加入26项接口。
   优先跟进实测中P2非人为掉线/序号重头：保留现场，用受限只读STATUS诊断或独立UART读取复位原因，并核对电源/NRST；不得直接归因供电或把短窗口稳定视为问题解决。
4. 主控0.9自身USB安装/END CRC/应用RUN/USB枚举和ECHO已通过；IR端口2、UL端口2正常全量升级已通过；其余端口与MOTOR、三类工厂镜像安装/回读、自动STOP/HOLD时序、各阶段断电、完整业务及真实停车仍待验收。native外接整链路到END，不执行ARM APP跳转。
5. 旧引导RUN/TC缺口已合入1.0.1修正，1.0.2保留该保护并修复EOPIE；旧1.0.0/1.0.1工厂文件不再用于升级。IR、UL已完成正常经主机升级实测，MOTOR仍仅构建及native验证；先核对其它板实际引导再升级，PC在BEGIN前拒绝低于1.0.2。原0.9合集中的旧factory/PC工具仅作历史证据。
6. 保留共享main的语音及环境材料，下一次合流逐项审查。提交只在本分支选择性暂存，确认无SDK嵌套仓库/生成物，再推送并比较本地HEAD与远端HEAD。不要直接在脏main切分支或git add -A。
