# 精简 Python 工作交接

工作区：`C:\Users\Administrator\.codex\worktrees\python-runtime\Codex`

基线：`63d1ea3`。该工作区从已提交的 `main` 创建，故意没有带入原工作区尚未提交的 UART4 语音代码；本轮也没有修改语音目录。

## 完成内容

- `bootloader/mini_python.c`：直接扫描 RAM 源码的固定内存解释器。
- `bootloader/klq_runtime.c`：积木 API、PC3、传感器缓存条件、显示和电机安全边界。
- `bootloader/user_program.c`：把 RUN/STOP/完成/错误接到解释器。
- `bootloader/main.c`：协作调度解释器，按运行状态切换 SC7/CS100A 采集率。
- `bootloader/protocol.c`：INFO 返回 `PY_ERROR/PY_LINE`，机器人固件版本升为 0.3。
- `docs/python_api_v0.1.md`：26 个非语音积木给上位机的唯一映射。
- `tools/test_python_runtime.py`：板上自动测试。

## 关键边界

- 用户 Python 只在 16 KiB RAM 中，复位/断电丢失。
- 语音积木不在本轮。
- 电机/红外协议未提供，`external_port_motor()` 为明确返回 false 的接入点，解释器报告错误 6。
- PC3 暂按上拉低有效，SC7 倾斜阈值 15°，方向需实物确认。
- 任何程序退出路径调用 `klq_runtime_stop_all()`。

## 当前阻塞

机器人 USB CDC 未枚举；DAPLink 的 pyOCD 只读连接卡住，且系统中存在其他项目占用 COM92 的监视进程。未确认 SWD 目标前不要使用 G: DAPLINK 盘烧录。

## 继续步骤

先完成 `docs/verification_20260921_python_runtime.md` 的实板步骤并回填结果。实板通过后提交本隔离工作区并推送；原 `F:\work\KLQ_V2\Codex` 有未提交语音改动，不要覆盖或清理，应通过提交/合并处理冲突。
