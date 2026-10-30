# 语音主机接入进行中交接（2026-10-30）

基线：C:/Users/Administrator/.codex/worktrees/python-runtime/Codex，codex/python-runtime@64837d5，开始时干净。共享F main@72e2ac4含其它任务未提交资料，不覆盖/切换。新任务来自CI对话用户授权通知，要求主机UART4联调；不自动回消息给来源聊天（未获回信授权）。

CI合同：F:/work/KLQ_V2/Codex/VOICE/docs/klq_voice_v0.2_protocol.md；配置VOICE/config/xiaoqi_commands.json；CI0.2.0已由语音任务写入，用户听音只确认开机/唤醒，本主机尚未验证。业务UART4 PC10TX/PC11RX ↔ CI PA3RX/PA2TX，1152008N1/3.3V。COM116是CI烧录日志口，不用于业务。主机此前0.10/USB引导0.3，主机本轮尚未烧录。用户已回复“已接好”USB、电池、CI供电/连线；先做无运动联调，电机动作重新确认机械准备。

VMUPV2：A5FC,lenLE16,type,cmd,seq,payload<=20,sumLE16,FB。sum仅type/cmd/seq/payload。GET_VERSION A1/94[8A] -> A2/94[v,0,2,0]。播放A1/92[80,90,voiceID LE32] -> A2/92[80]（不是00）；A3/9A[00,B3/B4]接受/SDK回调，不证明出声，事件不含seq/ID故播放序列化。A2/AA[FD/FC/FE,原cmd]错误。ASR A0/91 semanticLE32+00 00+score(7B)，回A2/91同seq[00]只表示收到。语义wake4B510001/fwd101/rev102/stop103。B0上电/B1唤醒/B2退出15秒。音效5000..5007开机/唤醒/接受/错误/程序开始/停止/完成/忙碌。

首版计划主机0.11：voice_link固定缓冲有界IRQ解析、版本/播放/统计/意图；voice_control执行瞬间EMPTY或READY+UISTANDBY+无升级/诊断/关机/故障，唯一身份匹配电机，有限1秒±800，normalport刷新<=100ms。接受需实际SET ACK后5002，拒绝5007，错误5003。运行中语音停止不终止用户程序；音效仍可播。工程运动使能默认0，先完成版本/音效/ASR无运动验收，再显式ARM+机械准备后实物。生产默认策略尚未决定。

分工（同一工作树，可见彼此文件）：
- voice_transport：只写voice_link.c/.h、自己的native测试/脚本。APIinit/poll/query_version/play/take_intent/drop_intents/take_events/get_status；KLQ_VOICE_TEST导出test_rx+txhooks。RX逐字节epoch标签避免busy旧ASR在闲时复活，保持播放ACK处理。
- motor_business：只写external_ports.c/.h及自己的native测试。normal真实SET/STOP、每次刷新前INFO锁UID、60ms刷新、150ms旧目标过期、SET不重试、STOP优先，独占OPEN需非阻塞STOP quiesce。APIcommand(port,int16PWM),ready/state/ack_pwm及motor_stopped辅助。旧power等级1..3映射333/666/1000，无助推。取消诊断亦需真实STOP，保留250ms接收端兜底。
- voice_policy：只写voice_control.c/.h+自己的tests。跟前两agent协调API，有限动作、freshgate、wakeexit/失效停止，默认运动disabled；cancel仅停止voice拥有动作，不干扰RUNNING程序自己的电机。提供sound队列、set_enabled/status及active。
- root：main/protocol/user_program/power_key/build.py、既有模拟tests脚本/适配、USB诊断CLI/观测字段、目标构建/map/实际安装与验收。不要重复改agent文件。

Root待集成：main保持所有原loop功能且voicepoll在外接独占continue之前；boot构建排除voice*.c。user_begin/start/stop/完成错误边界cancel；CMD_EXT_OPEN之前cancel+真实stop，USBreset只cancelvoice不能停独立RUNNING程序；globalSTOP/关机真实stop。关机1500ms识别后最多150ms quiesce，若全部STOP已确认可立即PC5；新增power_key_shutdown_pending。RESET同样延后quiesce且禁止此期间新运动。计划USB0x50状态/0x51版本/0x52播放/0x53运动使能/0x54取消，版本及播放非阻塞。cap EXT_VOICE=1/VOICE_MOTOR_ARM状态，USBINFO字节仍有界。

验收顺序：native黄金帧/噪声/坏和尾长/去重/超时/epoch；normalmotordriver及旧桥接303回归；policy完整门控矩阵；真实协议/按键/解释器共存、USB升级41、26积木；ARM APP/boot编译map和哈希，保留0.10恢复BIN（build/motor_live_20261029/app）。然后主机USB安装应用，不改引导或CI/MotorFW；版本往返→8音效ACK/B3/B4/用户听音→真实唤醒/三语义/ACK→运行/下载/诊断拒绝且音效可播→机械确认ARM后有限正反转和真实STOP。不得把模拟/ACK/事件当出声或运动。实板项尚未做。


## 后续实际进度（当前接续优先）

0.11已完成编译/安装：APP30664 B、RO30596/RAM27632 B，SHA9935b9f1a4ae6ee30c0bc943794995bd21a1e7b51900f86bd1b92b48ce5bccc0，较0.10+6260B Flash/+848B RAM。USB引导仍0.3，0.4引导仅重建9944B未刷。真实安装记录build/voice_integration_20261030/host_install.json，CRC/VALID/RUN/INFO/ECHO PASS，USER_RAM原先为空，默认voice enabled0/active0。安装前已按实际UID在P2电机STOP。

当前连线变为P1 IR/P2 N20/P3 UL，不能照搬此前P3电机。所有动作采用唯一motor动态选口。当前USB仍COM123（重查）。监测服务每阶段停止/释放串口后CLI独占，finally重启，最新PID在build/voice_integration_20261030/dashboard.pid；8音效后为6384（重查）。不要并行打开CDC。

实板无运动版本：tools/voice_usb.py version报告PASS，新的UART4查询收到CI0.2.0，见version.json/.log。8音效5000..5007逐个请求PASS，ACK均80，B3/B4每个+1、指定ID/空闲/无并发ASR/无错误条件通过，见sounds.json/.log；已向用户询问是否8段全部听到，**当前听音回复待到达**。不能把协议/SDK回调当出声。

软件回归：wire606、policy213、normalmotor341、旧bridge305、真实9模块共存342（新增PC4锁存回归）、USB395；实际USB/UART升级4链路和全部Python41无跳过；26积木22组164175断言。VC98既有转换告警保留，ARM无新增构建告警。Root修复审查问题：PC4按下时锁存停止动作，防止语音/诊断动作先结束后松键启动旧程序；非法OPEN无取消副作用；关机期间拒绝新入口；RESET设置pending后停止处理本批后续帧。150ms只是STOP尽力等待，不是精确物理停机保证。

USB诊断ABI108：0x50STATUS、0x51QUERY、0x52PLAY(idu32仅5000..5007)、0x53ARM(byte0/1)、0x54CANCEL。score96、intentSeq97、phase98、cancelled100u32、pendingSound104u16、pwm106i16，其余字段见tools/voice_usb.py decode。工具status/version/play/sounds/arm/cancel/watch；sounds先显式ARM0且确认active0。版本/音效等无运动。watch默认30s/上限120s，按语义/计数变化打印并保存JSON。声音完成条件严格ACK80/请求ID/idle/ACK+B3+B4计数模差/无新超时/错误/ASR。不要使用事件mask供多个消费者，policy单独消费take_events，诊断只读累计计数。

下一步：听音回复后，先保持ARM0开始约40s真实ASRwatch，提示用户说“小奇小奇→电机正转→电机反转→停止电机”，核对B1/B2、4种semantic/score及禁用拒绝，不注入词代替实物。再重新确认机械准备，才ARM1；在RUNNING/DOWNLOADING/独占分别实际拒绝且音效能播，再测EMPTY/READY真实有限正反转/STOP和取消。程序运行中语音STOP不停止用户程序。所有真实语音/动作验收项目前未做。

所有0.11代码/测试/文档仍是64837d5之上的未提交更改，不得称已同步远端。共享F的VOICE SDK/模型资源与本对话代码隔离，CI本轮没有再刷，COM116不访问，不擅自回发消息给CI聊天。


## 再后续实际进度

用户第一次8音效未注意，要求重播；sounds_repeat.json全部8ACK80/B3/B4PASS，用户明确“8段都能听到”。真实ASR禁用测试asr_disabled.json 45秒捕获wake/fwd/rev/stop四语义，B1/B2+1，score有效，accepted0/rejected3/enabled0/active0，用户回复“已说完，但电机没动”，已解释这是禁用验证。

用户重新确认“N20仍悬空，可以开启测试”。第一次arm_idle.json在首个INFO写入就WriteTimeout，ARM未发送。强制kill监测后CDC重新打开出现写超时，COM仍枚举、重启监测反复失败。已停止坏监测24636，用户仅拔插主机USB（电池/CI保持），直接CLIstatus恢复，main/CI状态均保留，enabled0/active0，错误0。未定位最终根因，不能称主机硬件或ASR错误。voice_transport agent正在为监测增加优雅POST pause/resume，避免强制终止重叠IO；尚未部署。

为继续受控测试，private voice_idle_live.py维持同一Serial连接：先ARM0/核对P2UID/真实STOP，等待发现，版本查询，再ARM1，60秒收集真实语音。结束finally ARM0+HostLink核对UID/真实STOP ACK（不依赖仅desired0），监测在此期间未运行。接下来将提示用户“小奇小奇→电机正转→等停→电机反转→等停→停止电机”；正反转仍80%最多1秒。实物该阶段尚未开始。成功/失败详见asr_armed_idle.json，若最终USB写失效要告知用户并确认物理停止，不能盲继续。


## 用户暂停最终状态（本节优先）

用户“电机没转，先暂停其他测试，等会再来”。从该请求起没有再运行其他测试。gate_running PASS：真实正转/停止BUSY拒绝，accepted0/rejected+2、无活动电机，语音STOP没有结束RAM数字12程序（USER_STATE3/UI5）；运行期间5003 ACK/B3/B4通过。用户仅确认电机未转，未单独确认数字仍显示。finally ARM0/active0、空RAM、精确P2 UID后的真实STOP ACK，cleanup_pass=true。

监测恢复后再次连续USB Write timeout，firmware={}、voice.supported=false；connected=true只是端口刚打开，不能称正常。暂停收尾POST/api/pause得到200/released=true，服务/页面保留、自动重试停止。前文“优雅交接成功”仅指释放/重开流程，不表示通信持续正常，根因待下次查，不在暂停后继续诊断或要求拔插。

此前8音效重播用户全听到；asr_disabled四语义/B1/B2通过；asr_armed_idle用户“都正确”、±800、accepted+3/completed+2/error0及真实STOP ACK。停止词是在有限动作结束后说的，不证明活动中打断。gate_ready/downloading/diagnostic均未运行；PC4等活动取消/150ms关机复位物理时序待测。

当前C工作树codex/python-runtime@64837d5之上源码仍未提交/未推送，F共享main未修改。完整暂停记录voice_integration_paused_20261030.md/json。当前不自动恢复监测串口，不自动ARM；下次先核对分支/提交/工作区、USB状态和机械准备。


## 恢复后最终接力（优先于暂停节）

本轮五项真实语音/USB STOP/PC4验证已PASS，用户均确认，主机随后USB更新0.12完成RC_W0小修复（增加40 B Flash、RAM不变），20串口开关/240边界ECHO/5监测交接及CI/RAM复用通过。完整字段和证据见verification_20261030_voice.md/json；目标源与模型见build/usb_ep_fix_20261030/build_evidence.json。暂停期USB_RESETS4→恢复6，不能说只等待就恢复。普通Python电机实物、物理拔线及150ms关机复位、长期稳定性仍待测。0.11脚本的严格版本检查须改为0.12并更换输出目录才能复测，避免覆盖原证据。
