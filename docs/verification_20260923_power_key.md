# PC4 短按运行／停止实板验证（2026-09-23）

目标为 APM32E103RET6，机器人 USB 端口当时为 COM123。系统固件 `KLQ ROBOT FW 0.4` 经现有 USB 引导下载，`END` 完整镜像 CRC 校验通过。板上固件对应二进制 20104 字节，SHA-256 `7E16F6EF9EED17B94CAEFCC7D31A4079D2132ABC13B18143BBFE46797BD0875D`；链接映射 RO 19824 字节、RW 26440 字节。引导区本轮没有更新。

解释器和 RAM 用户程序回归脚本 `tools/test_python_runtime.py`、`tools/test_user_program.py` 均通过。随后把 `tools/klq_display_demo.py` 的 107 字节源码重新下载到 RAM，未附带 `RUN`：`USER_STATE=2, USER_VALID=1, UI=1, PK_SHORT=0`。

用户在第一次观测前手动短按两次，状态回到 `USER_STATE=2, UI=1, PK_SHORT=2`。之后又手动短按几次，观测到 `USER_STATE=3, UI=5, PK_SHORT=5`，程序运行中交替显示数字 12 与开心表情。明确要求不碰按键后，连续 8 次 INFO、约 8 秒内 `PK_SHORT=5` 未变化。用户再短按一次，`PK_SHORT` 从 5 到 6，`USER_STATE` 从 3 到 2、`UI` 从 5 到 1，`USER_VALID=1` 保持有效，可以再次运行。

曾因不清楚期间的手动按键，把累计计数变化误判为按键重复触发；用户随后澄清期间确有手动按键。临时增加的 80 ms 最短按压和 500 ms 防连按代码未烧录，已撤回；最终源码与实板均保留 25 ms 去抖、1.5 秒长按不生成短按、开机同次按压不生成短按。长按关机尚未实现或验证。PC4 有效电平目前按上拉、按下为低实现，实物短按运行／停止已验证。
