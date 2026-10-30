# 蓝牙开发暂停交接（2026-10-30）

用户明确今天停止工作，之后继续；并指定保存同步完成后关闭这台电脑。已停止所有代理/蓝牙扫描，没有进行新的蓝牙连接或固件测试。

## 当前实板

- 主控APP仍为稳定0.14；新APP0.15已编译，尚未安装。现已USB RUN回0.14双眼待机，RAM空、PY_ERROR0、SYS_WDT1、电机停止、语音ARM0，观测页已恢复。RUN切换时旧串口句柄在ACK消费前断开，未盲重发RUN，只重新INFO确认0.14成功，见pause_cleanup.json。
- 驻留BOOT已由0.3更新为0.5（14872B，SHA e32ac440683c65daa7e6f7d5ee7cf6fb080207d919b5fc6db75b7490f4b3d6ef）。整个BOOT32KiB逐字节符合预期，APP/元数据/其他Flash及选项字节均与完整备份一致。
- ECB02C配置已实测：Peripheral、KLQ2、MODE2纯透传、SLEEP0、ECHO0、CONNOTIFY0、BTAT0、FFF0/FFF1/FFF2；5次差异设置后RST重查通过，模块固件12.2.13，MAC EC:23:09:10:4E:9C，ready/configured1、UART错误0。用户手机BLE工具能搜到KLQ2。
- 电脑Win10/Intel蓝牙正常，能扫到其他BLE广告，但两次10s未发现目标，故尚未实际GATT连接机器人，更没有完成电脑BLE INFO/ECHO、RAM下载或蓝牙固件升级。不要把“手机可见”或native单测写成端到端成功。
- 当前APP0.14没有BLE协议处理；只有新BOOT0.5具备BLE通道。下次先USB RESET进入BOOT0.5，解决PC发现/连接，再验证BOOT GATT，随后蓝牙下载APP0.15并验证RAM用户程序。

## 已完成源码与验证

APP0.15 BIN36588/RO36520/RAM31256，SHA ecdc628f5a99ce6f231901abb1598f30ddc48624335fa0f86b7cc8365d4864b7；BOOT0.5 RO14580/RAM12344。两路独立固定parser/cache/response、单下载owner、非owner断连不清他路半包、STOP全局、跨路pending RUN屏障。USB/BLE在原子消费reset时释放TX借用，防urgent ACK被新STOP覆盖。UART5 PC12/PD2、PA8连接状态；无虚构模块控制GPIO。无堆，原地址/元数据布局保持。

真实C BLE18553断言+8负对照，双路APP711/BOOT466+14负对照，USB718/business342/pipeline4，PC fake GATT21及独立reset竞态回归通过。ARM有原2B literal pool对齐和一条assignment条件提示，无错误。规格/ABI见bluetooth_transport_v0.1.md，工具与依赖见bluetooth_pc_client.md。以上不替代真实射频/断线/升级验证。

## 恢复与原始证据

主控UID0044003A/3531330D/4341364B，M3/512KiB；DAP LU_2022_8888、Geehy DFP1.0.3。

完整原Flash before_ble_full_flash.bin SHA5d86058efc7fe573b94ad1b11eba1c6b34a1d05d5f7bbc2ac35b9f1557808d7d；原BOOT前32KiB before_ble_boot_0_3.bin SHA9d11e69da5d3f846037815eef31f98f0eaf6b3aba7945d545730354f1fcc9f07。新BOOT写后全Flash post_boot_full_flash.bin SHA1068f3b175257b552c8c9db2c3747caf47df208e536dbdf43431937165db9511，protected_range_matches和option_bytes_unchanged均true；options始终a55affffffffffffffffffffffffffff。

APP0.14直接HALT长读受看门狗干扰，改为先进入原BOOT0.3后完成稳定完整备份。探针曾卡DAP_INFO，已用户重插恢复。首次preserve-power-halt Flash算法报page0 result1；下一次未先reset读系统区域出现旧FLM状态。**必须入口先SYSRESETREQ+halt、PRIMASK1/停SysTick/清NVIC/确认Thread0及PC5高**，实际install_boot_reset_repair.py重试成功，完整保护区和读回验证通过。失败记录保留；当前引导已新版本，不能重跑只接受旧BOOT的初装脚本。

原始目录build/bluetooth_20261030，PC环境/扫描build/bluetooth_pc_20261030，双路报告build/dual_protocol_20261030，独立审查build/bluetooth_pc_review_20261030。pause_cleanup.json确认已回APP14；read_ble_status.py为BOOT0.5的0x56读工具，不能在APP14调用。AT逐字SWD注入未得到回复，不作为参数证据，不再继续；RAM助手未创建/执行。

## 下次继续

1. 读本文件及handoff，核对C权威codex/python-runtime分支；F共享main及UL/VOICE等脏工作不覆盖。
2. 重启电脑后需重启tools/status_dashboard.py服务，先查COM而非硬编码123；当前deps隔离在build/bluetooth_pc_20261030/deps，requirements_ble.txt可恢复。
3. USB RESET→BOOT0.5并确认BLE状态；手机退出可能的KLQ2连接，PC用主动BLE扫描检查MAC/广播/中央适配器与缓存。手机已能见到，优先排PC发现侧，不盲改芯片/RF或固件。
4. 只有确认目标后运行live_ble_checks.py的boot阶段，只读INFO/ECHO。成功后CLI flash APP0.15 --run（只APP、不碰BOOT），再app阶段下显示12、断连保留/STOP/半包取消，并保持USB观测独立。
5. 无需电机运动或重复询问悬空。未证明项目完成；收尾保存报告/版本矩阵、更新交接和remote。

电脑关机使用正常系统关机，不加强制关闭参数；机器人未要求关机，仍保持待机。
