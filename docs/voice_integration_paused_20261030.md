# 语音接入暂停检查点（2026-10-30）

用户要求“电机没转，先暂停其他测试，等会再来”。从该请求起不再启动其他测试。

- 安全收尾已经完成：ARM0/active0，真实P2电机STOP ACK，数字12的RAM测试程序清理为USER_STATE0/USER_VALID0。证据gate_running.json，cleanup_pass=true；cleanup中的STOP是查询精确UID后发送0x302并收到ACK，并非只看主机状态。
- 监测页面/服务保留，但恢复串口后再次出现Write timeout，无有效INFO/voice快照。不能把connected=true当作正常。已POST /api/pause得到200、released=true，停止自动重试。先前“优雅交接成功”仅说明释放/重开流程完成，不能证明USB持续通信正常。下次处理该问题，本轮不要求用户再拔插。
- 当前主机已安装0.11，常驻USB引导0.3保留。CI0.2.0、电机2.0.0本轮未刷。P1 IR/P2 N20/P3 UL；按UID识别，不能固定认旧端口映射。
- 权威源码在C:/Users/Administrator/.codex/worktrees/python-runtime/Codex，codex/python-runtime@64837d5之上的未提交WIP，本轮尚未commit/push；F共享main包含其他任务工作，不切分支、不覆盖、不混合stage。

已验收的范围：

1. 主机USB安装、CRC/RUN/INFO/ECHO，CI UART4真实版本0.2.0往返。
2. 8音效逐个ACK80、B3/B4；首次用户未注意，重播后用户确认8段都听到。
3. 真实四语义、小奇唤醒/退出事件观察；ARM0时拒绝运动。
4. EMPTY空闲ARM1后真实正反转各最多1秒、PWM±800，用户确认方向/停止都正确；accepted+3/completed+2/error0。停止词是在有限动作结束后说的，尚不能宣称打断活动动作。
5. RUNNING数字12/无延时无限循环时，真实正转和停止均BUSY拒绝，accepted0/rejected+2，无active；用户确认电机没转。语音停止未结束用户程序，板端前后仍USER_STATE3/USER_VALID1/UI5。用户没有单独确认数字12持续显示。该状态中音效5003 ACK/B3/B4通过，随后安全清理。

待恢复后完成：USB恢复后写超时根因；READY已下载程序空闲接受、DOWNLOADING/诊断排他实物拒绝；活动动作下语音STOP、PC4/USB停止/断连/退出唤醒；150ms关机复位等待时序；长期及精确停止延迟。相关native覆盖不替代板测。私有voice_gate_live.py准备了四模式，只运行running。

软件检查已通过：wire606、policy213、normal motor341、bridge305、九模块342、USB395、Python41无跳过、26积木164175断言。APP30664/RO30596/RAM27632 B，相较0.10增加6260/848 B，无堆。APP SHA9935b9f1a4ae6ee30c0bc943794995bd21a1e7b51900f86bd1b92b48ce5bccc0，08008000..0800F7C7；引导回归9944 B未安装。

工程语音使能默认0；USB0x50..54提供108B状态、版本、播放5000..5007、ARM和取消。音效接收确认不等于实际出声。状态中的原始接收语义与最近策略处理score/seq须分开看。执行门控为EMPTY或READY+UI STANDBY+无错误/下载/独占/关机/复位；唯一身份匹配电机。普通电机驱动已替换stub（60ms刷新、150ms旧目标过期、SET不迟到重试、STOP优先），直接Python电机积木尚未实物验收。

完整过程/API/脚本见voice_integration_worklog_20261030.md；机器记录voice_integration_paused_20261030.json。原始证据及0.10恢复BIN在build/voice_integration_20261030。CI模型/SDK仍在F共享区另一个任务中，本分支clone不能单独恢复CI构建。未获授权向来源CI聊天回复，因此没有发送。
