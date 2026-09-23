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

## 实板状态

2026-09-23 机器人 USB COM123 已恢复，新系统固件 0.3 经 USB 引导写入并通过 CRC，解释器与 RAM 下载回归均通过。调试日志已改成短超时，以免无人读取串口时拖慢用户程序。最终镜像 19,660 字节、SHA-256 `B72BBAD1722E9FA63440E708186C1B352706A91F66CC9FCDECAB40EC3C80F5A9`；详见 `docs/verification_20260921_python_runtime.md`。无外接传感器，CS100A 实测未进行。

## 继续步骤

人工确认点阵图案、PC3 按下/释放和 SC7 四方向；取得电机/红外真实协议后实现 `external_port_motor()` 与红外采集。原 `F:\work\KLQ_V2\Codex` 有未提交语音改动，不要覆盖或清理，应通过提交/合并处理冲突。
