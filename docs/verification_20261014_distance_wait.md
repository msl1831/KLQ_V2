# 距离积木有效样本修正与实板验证（2026-10-14）

目标为 APM32E103RET6 主机。机器人 USB CDC 枚举为 COM123；CS100A 独立供电，主机端口 2 识别为 `device_class=1, sensor_type=1`、UID `4150354A/38343111/0080185B`。监测日志报 `sample_status=1`、距离约 5601 mm、端口在线。依据 `UL/docs/klq_sensor_protocol.md` 的 `GET_SAMPLE` 定义，状态 1 才是有效样本，0 为无样本，2～5 分别为近距、超量程、无回波和故障。

旧系统固件 0.7 的 `klq_runtime_distance()` 要求 `!sample_status`。下载 RAM 程序 `import klq\nklq.wait_distance(2, 1, 100)\nklq.display_number(7)\n`，在有效且大于 100 cm 的样本下运行 0.4 s，`INFO` 仍报 `USER_STATE=3, PY_ERROR=0`，确认条件错误地不成立。修正为只接受 `sample_status==1`，保留端口类型、在线、80 ms 新鲜度及严格比较检查。

ARMCC5 构建 `python tools/build.py --demo` 通过，最终系统固件 BIN 20412 B，SHA-256 `1BE677DB4FD6854F60B6B8BC90BF1A46C4EFBB3282BF50ED453A47025F358ECF`；map 中 RO 20128 B、RW 26448 B。经机器人 USB `reset` 进入 `KLQ USB BOOT 0.3`，`flash ... --run` 的整包 CRC 校验通过；重新枚举后 `INFO` 为 `KLQ ROBOT FW 0.8; VALID=1`。引导镜像未更新，未通过 SWD 独立读回系统固件；板载完整性证据限于引导 CRC、有效标记及运行。

最终 0.8 上重复正向条件，0.4 s 后 `USER_STATE=2, USER_VALID=1, PY_ERROR=0`。修正版上另测 `klq.wait_distance(2, -1, 100)`：条件为假时仍运行，USB `STOP` 可使 `USER_STATE=2, PY_ERROR=0`。随后经引导重新启动系统固件，确认 `USER_STATE=0, USER_VALID=0, UI=1`，测试程序未留在 RAM。

本次只证明有效样本状态与积木条件判断一致；没有用标尺验证 5601 mm 的测距精度，也未覆盖状态 0/2/3/4/5 的实物注入。监测页和串口客户端不能同时占用 COM123；测试时暂时停止监测服务，完成后恢复。
