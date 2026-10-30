# USB事务与脱机运行（0.14，2026-10-30）

正常机器人程序通常脱离USB运行。USB是下载/工程观测通道，不作为普通Python程序和其电机动作的保活条件。USB总线reset、CDC关闭或RX溢出取消未完成下载、USB排他诊断/升级及语音工程动作；完整RAM和普通Python运行保留。明确STOP、合法新BEGIN、RESET仍按原规则停止旧动作。固件无法只凭VBUS准确识别所有电池供电下的物理拔线，不能把无总线事件等同于已经检测到拔线。

`usb_serial_write`只表示接收发送请求，不代表主机已收到。调用者保留缓冲到`usb_serial_tx_state`为DONE或FAILED。`usb_serial_poll`每次最多推进一个63字节短包，不忙等，不调用Python/按键/端口等业务函数。单个帧最长250 ms；超时NAK并清旧TX完成标志。最后IN完成中断才置DONE。一个固定TX指针复用协议原response缓冲，无堆或完整回复副本。

协议仍要求单个未完成RPC。慢读期间保持在途回复不变，CRC合法的STOP、VOICE_CANCEL、VOICE_ARM(0)仍立即执行，另有20字节紧急ACK槽；其他并发请求不执行、不排队，等待上位机超时重试。紧急ACK也有且只有一个槽，槽已在途时后续不同STOP仍能执行，但其回复需重试。不能据此宣传任意流水线请求或无界回复队列。

RESET和BOOT RUN的动作随回复缓存保留，只有真正TX DONE后提交一次。首次写失败或异步发送失败后，同seq重试可完成尚未提交的动作；普通DATA/RUN等已执行副作用仍由原事务缓存抑制重复。BOOT请求保留标记推迟到提交，不在失败ACK前留下启动标记。新普通事务/USB会话结束丢弃未确认的旧动作。明确RESET本身仍先停止业务，再等待ACK。

INFO只读取固件有效性缓存。首次状态查询若尚未预热会验证一次；启动可调用`protocol_system_firmware_valid`预热。BOOT启动、END和RUN保留完整镜像验证，合法BEGIN在擦写前失效缓存。突然的运行时Flash损坏不由INFO热路径重复扫描发现。

二进制RPC只要求USB已配置，不要求DTR，以兼容未显式置DTR的现有上位机。CDC配置且DTR打开时才允许日志。`usb_serial_console_active`可在格式化前判断，`usb_serial_log_ready`进一步要求TX空闲。日志至多160字节，复制到固定槽，发送期间原格式化栈可复用；协议回复优先于下一条日志。超时后日志静默到新DTR/reset，二进制RPC仍可在同CDC会话重试恢复；DTR下降仍取消在途发送/会话，之后DTR=0的新RPC也可应答，且不自动开启日志。主循环须对端口日志轮询轮转，每轮只构造一条，否则单槽可能持续只显示第一个口。

新增固定存储为160字节日志槽、20字节紧急ACK、TX游标/状态及事务/有效性状态。已生成的ARM链接map相对0.13显示usb_serial.o的RW+ZI从4292增至4472（+180）字节，protocol.o从2124增至2148（+24）字节，合计RAM+204字节。没有新堆分配。

原生验证：`sensor_boot/tests/usb_host/run_tests.ps1`运行实际protocol.c的应用协议497断言、引导RUN/RESET/INFO缓存15断言和实际usb_serial.c的异步驱动703断言（含DTR=0二进制发送完成、日志仍静默）。另业务9模块342断言、PC→实际C USB/UART→三种外设完整链路4项通过。均未访问设备、串口、探针或目标Flash；host mock不能替代Windows CDC/DTR、拔线、真实IN完成时序和电机连续运行验收。APP 0.14使用这些修改；已驻留主机BOOT 0.3没有被这次APP更新自动升级，BOOT源码/回归与部署必须区分。
