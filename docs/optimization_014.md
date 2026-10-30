# 主控0.14优化与验证（2026-10-30）

已通过主机原生USB安装应用0.14；驻留BOOT0.3和IR/UL/MOTOR/CI固件保持原样。基于0.13架构审查整改，正常使用以独立电池供电、USB仅用于下载/按需观测为准。Python仍为16KiB RAM文本，不写内部或外部Flash；下一次BEGIN覆盖，复位/断电失效。

## 调用与保护边界

不要求必须接任何外设，三个端口可各接同类传感器或电机。仅当前程序实际调用的目标端口及其已启动电机影响该程序健康：未使用空口/其他口错误不造成退出。固定双电机移动积木明确使用P1/P2，语音工程命令因未指定端口仍保留单电机唯一性门禁，不妨碍Python逐口控制。

wait_distance/tilt区分条件未达与设备故障。距离样本状态1有效时做严格比较；新鲜状态2/3/4（过近/超量程/无回波）继续等待；缺失/错型/采集过期/硬件错误连续250ms则PY_ERROR7、停止程序及电机并保留源码。距离/IMU新鲜阈值仍为80/100ms，未使用传感器不检查。wait_button继续合法无限等待。电机启动时缺失/不兼容报6，已拥有电机异步失联/错误报7。参数错误保持原错误码。

## 对应审查修复

| 事项 | 0.14行为 |
| --- | --- |
| A1 失败应答后的复位动作 | 回复缓冲持有到真实TX DONE；RESET/BOOT RUN待提交动作随同seq缓存保留，重试后只提交一次。已执行DATA等不重复副作用。驻留BOOT0.3仍为旧实现，源码BOOT0.4仅回归构建。 |
| A2 USB阻塞/离线开销 | 每次只推进一个≤63B短包，无忙等。整帧250ms超时；STOP在慢回复期间仍执行。固定160B日志槽和20B紧急ACK槽。无配置/无日志会话不格式化；无detach通知时在途日志超时后静默。普通Python无需USB保活。 |
| A3 旧测量假新鲜 | 首样本标记；sequence和capture都改变才更新测量/年龄，重复响应仅证明通信。超时先于RX解析，丢弃积压，正常重试换序号。 |
| A4 条件故障策略 | 上述目标资源三态等待与250ms故障宽限。正常无回波和合法无延时循环不算程序错误。 |
| A5 租约恢复次序 | 主循环先端口后VM；ready/state/command入口也检查150ms旧目标租约。超时锁存fault+STOP，新方向不能偷偷授租复活；显式STOP/新RUN清理后再运行。 |
| A6 语音去重 | 去除策略层单8-bit seq误拒，保留传输层完整tuple/epoch去重，B0/回绕受测。 |
| A7 异常恢复 | 软件IWDT标称2s，关键主循环返回后每250ms喂；非ISR盲喂。强Fault向量切换安全栈、记录原因/PC/LR/CFSR及校验后请求BOOT复位。持用户键启动约40ms可进下载引导。 |
| A8 INFO CRC | APP启动预热一次，INFO读缓存；BOOT启动、固件END/RUN仍完整验证，固件BEGIN先失效缓存。 |
| A9 关机延迟 | 立即detach和重枚举300ms等待分离；STOP ACK/最多150ms后走显示熄灭和PC5低，不再为Windows额外等300ms。USB/DAP旁路下关机GPIO循环专门喂狗。 |
| A10 RUN上下文 | 每次RUN默认motor/move功率2，不继承上次3；保留后台缓存和按键状态。 |
| A11 行号 | CR/CRLF/LF按同一行计数规则报告PY_LINE。 |
| 显示/采集 | masked13B帧去重；同图案不重复整屏位操作。外接20/40/100ms周期从请求发起算，迟到跳过，不追赶突发；运行升频及时。 |

二进制RPC只需USB已配置，未置DTR也能回复；DTR只门控日志，兼容现有上位机。单个未完成RPC的规则不变，其他并发请求不排无界队列。普通USB reset/CDC close不清完整RAM、不停自主Python；取消未完成下载、USB排他工程会话及工程语音动作。工程STOP/RESET/合法新BEGIN仍停旧程序。详见[USB实现说明](usb_async_014.md)。

## 资源与恢复

APP BIN32980B，RO32912B，RW/ZI27888B，相对0.13增加2028B Flash、256B常规RAM；含16KiB用户源码和2KiB栈，没有新堆分配。另额外保留24B Fault记录于0x2001FFE0..0x2001FFF7，原BOOT请求8B位置不动。应用SHA256：`d6bbf4a0e12f3b67bb4b2efc6347cd4254af420f518be061e29c19ab779c067f`。

BOOT源码回归10660B/RO10424B/RAM8968B，SHA256 `4784da0761a9d04696840ef6c5b24fa34c3556fdc0505c4e70ad86c1777f8877`，未安装。ARM汇编有一条2B literal-pool对齐提示A1581W，编译0 errors；所有Fault向量已核对指向board.o强实现。HEAP512声明原本就未链接，未虚报移除收益。静态调用图不等于完整IRQ/实际栈水位上界。

IWDT第一次实装发现SYS_WDT=0；记录保留在`host_install_first_guard_disabled.json`。按芯片实际同步要求改为先CCCC启动、再解锁配置和等待STS、最后AAAA重装后，SYS_WDT=1。若已启动后配置超时，转BOOT复位，不能静默返回假装没启用。未改选项字节。依据[APM32E103官方手册](https://www.geehy.com/uploads/tool/APM32E103xCxE%C2%A0User%C2%A0Manual%C2%A0V1.6.pdf)§5.2/18，并实测启用后SYSRESET到驻留BOOT0.3稳定6秒、再RUN应用正常。没有人为停喂狗或触发真实HardFault；native不能替代此类芯片故障注入。旧BOOT3会清reset flags，不能保证应用诊断能追溯看门狗复位原因。Fault后的电机依赖从板失联保护，不宣称电气即时失能；HSE/PLL启动故障、电池/MOS电气和从板硬件看门狗仍需单独验证。

## 验证证据

| 范围 | 结果 |
| --- | --- |
| 实际C USB/BOOT/异步驱动 | 497/15/703断言；含deferred retry、慢收STOP、cache、DTR0 |
| 积木/mini运行保护 | 164175断言；666运行断言和4个必须失败的负对照 |
| 端口/普通电机/升级桥接 | 1150/341/315断言；USB→UART→三种从板完整链路4项 |
| 语音与点阵 | policy222、wire1131；TM1640 APP/BOOT各6464 |
| 实际board C提取 | APP126/BOOT85；13个破坏保护的负对照均捕获；不验证真实MMIO/ARM入口 |
| USB端点/PC页面 | 39321600种寄存器模型及负对照；连接8、motor snapshot8、live工具14项 |
| 主机USB安装 | END CRC/VALID/RUN/INFO/ECHO，SYS_WDT1、BOOT3稳定6s、RUN通过；仅APP擦写 |
| CDC/RAM | 完整READY/RUN源码关闭重开保留、STOP后保留/可重RUN、半包取消、无延时循环不中断系统服务 |
| 电机 | P2 timed正反各0.8s正常UART ACK，停止在诊断前确认。1秒有界程序在CDC关闭约半秒后仍ACTIVE+ACK1000，重连STOP归零；本轮未另要求用户逐次观察机械方向。 |
| 目标口错误 | P1为IR时wait_distance明确调用P1，约250ms报7/第2行；motor_run(P1)报6/第2行。之后下载/运行正常，完好源码保留。 |
| 长按开关机 | 用户确认：长按关机后等待至少3s不自启，再长按开机正常双眼待机；RAM按复位设计清空。电气PC5/MOS微秒级时序未测。 |
| 不置DTR | 真实Windows CDC DTR0下INFO及1024B ECHO成功，日志仍静默 |
| 物理拔线 | 用户确认：电池不动，拔主机USB数字12保持，PC4停止到待机、再启动12，接回USB均正常；重连INFO RUNNING/valid1/PK_SHORT2/错误0/WD1。 |

原始证据在`build/optimization_0_14_20261030`及其交付副本；native专项在`build/opt_ports_014`、`optimization_runtime_20261030`、`optimization_system_guard_20261030`、`tm1640_cache_20261030`等。不能把PC模拟、普通UART ACK和人的物理观察混为一种证明。

本版不扩展Python变量/if/异常语法、蓝牙下载、wait_ir、菜单或电池ADC；这些仍是独立功能待办。上位机继续使用原积木函数和KLQ1 BEGIN/DATA/END，新增诊断SYS_FAULT/SYS_PC/SYS_WDT可按需展示。实际后台Hz和最坏调度延迟需用请求/响应时间测量，不以页面刷新或传感器内部seq跳数替代。

末次状态：用户完成关机/开机后，监测自动恢复，APP0.14/WD1/错误0，RAM EMPTY、三口在线，P2电机goal/ACK均0、工程语音ARM0。

PC侧10次INFO往返中位22.94ms、最大24.98ms，ECHO中位14.91ms、最大24.17ms，包含Windows/USB/串口延迟，不是主循环或传感器帧率。无0.13同条件基线，不给出提速百分比。
