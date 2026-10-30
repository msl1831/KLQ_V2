# Python电机积木进行中交接（2026-10-30）

基线：codex/python-runtime@640842c（C:/Users/Administrator/.codex/worktrees/python-runtime/Codex），开始干净。F共享main不修改。用户明确“电机一直悬空，后续测试时不用再问”；本轮起常规短转不再重复机械确认，必要时仍确认实际动作结果。已告知Python功率3=100%，不是此前语音80%。P2 N20精确UID4150354C/38323609/00D86C62，P1 IR/P3 UL；主机0.12，ARM0/空RAM，监测20812（重查）。

0.12基线实物：RAM代码power3，正反motor_for各0.8s，中间等待0.8s；程序无PY_ERROR、自然READY保留程序，用户确认“正常”。证据build/python_motor_20261030/baseline_0_12.json。结束已ARM0/清RAM/真实motor STOP ACK。旧固件没有普通电机只读反馈，不能把diagnostic OPEN后0当作正常定时停止的协议证据。

发现并正在修：异步SET/续期/身份故障未传给等待中的Python。agent python_motor_preflight拥有klq_runtime/mini_python及相关mock，增加3-bit所有权和pending前故障检测，错误7退出stop_all，不改计时/功率。agent python_motor_snapshot拥有protocol/external_ports/status_dashboard及USB测试，增加只读0x55（19 B ABI1+三口6 B state/online/goal/ack）与EXT_MOTOR_STATUS能力，不发UART不改会话；0x55 state枚举0IDLE/1PENDING/2ACTIVE/3STOPPING/4ERROR，与online组合看。root拥有tools/test_python_motor_live.py、版本、构建/烧录和文档。

待两agent完成，root改主机0.13，构建检查map，USB装应用（保留boot0.3及外设），板测timed/continuous/program_stop/natural_end/runtime_error/usb_stop。新runner源码每次输出均有限0.4～1s，电脑退出也不会留下无限用户运动；通过0x55在任何diagnostic OPEN前验证普通goal/lastACK归零。0x55是缓存UART ACK，不是编码器转速/引脚时间。故障watch补充native须和旧26积木/九模块/USB/pipeline合流回归；尚未完成不可宣称0.13已安装。

常规串口所有权：POST8765/api/pause 200/releasedtrue→单CLI→close→resume。监测加载PC新代码须先确认released且精确核对PID，禁止带串口强杀。细节及现阶段源码/工具皆未提交，勿被旧hdd0.12完成记录误导。


## 当前状态更新（本节优先）

0.13已USB安装，APP30952/RO30884/RAM27632 B，比0.12 Flash+248B、RAM不变。SHA6637592656fdc0ebc5b681cb569433e5f1c40f9317c038ee342db9edd6d690ae，boot回归9984B/哈希同0.12，未写引导/外设。fault188+负对照/26积木164175，USB441/9模块342/pipeline4/PC快照8+连接8/工具14 fake已通过。

首次0.13 automated timed协议PASS，但用户未注意。repeat start-key窗口60s超时（窗口内PK_SHORT始终0）；清理时之后用户才按，RAM空导致X，不能记作电机故障或实物通过。后改持续保留RAM、不超时清理、由用户随时PC4开始，HTTP仅观察不写设备。

persistent_timed_observed PASS_PROTOCOL_OBSERVATION，用户“正常”：+1000/0/-1000/0，RUNNING→READY，RAM有效。随后persistent_continuous_observed同样协议+用户正常：motor_run/wait/motor_stop正反各0.8s。当前RAM保留continuous示例、已停、语音ARM0，监测新PID在build/python_motor_20261030/dashboard.pid（重查）。下一步四个自动短动作case program_stop/natural_end/runtime_error/usb_stop；runtime_error会故意参数方向0报告PY4（第6行），不可误作缺陷。根机计划全部完成后留下一个有限示例在RAM，用户短按可重复观察。


## 本轮完成（本节优先）

四个自动case program_stop/natural_end/runtime_error/usb_stop均PASS，均在任何diagnostic OPEN前普通ACK归零保持0.3s，分层清理/功率2恢复通过。用户反馈“有短转和随后停止，没注意次数”，不写成四项逐一实物确认；timed与continuous已分别完整确认正常。当前RAM保留examples/motor_blocks_demo.py组合有限示例，READY有效未自动RUN、goal/ACK0、语音ARM0。完整记录verification_20261030_python_motor.md/json；进一步其它口/档位/双电机、实际异步拔线PY7及电气时序未做。代码、文档准备选择性提交并推送独立分支，不覆盖共享main。
