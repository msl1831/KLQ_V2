# KLQ 精简 Python 首版验证记录

构建日期：2026-09-21；实板测试日期：2026-09-23

## 构建结果

- 系统固件：`build/klq_demo_app/klq_demo_app.bin`
- 链接地址：`0x08008000`
- 文件长度：19,660 字节
- SHA-256：`B72BBAD1722E9FA63440E708186C1B352706A91F66CC9FCDECAB40EC3C80F5A9`
- map：RO 19,396 字节，RW 26,424 字节，ROM 19,660 字节
- 引导回归：`klq_bootloader.bin` 9,280 字节；`mini_python.c` 与 `klq_runtime.c` 未链接进 32 KiB 引导；本轮未改写板载引导
- Python 主机工具通过 AST 语法检查，`git diff --check` 无空白错误

## 已实现并由构建覆盖

- 16 KiB RAM 源码直接解释，不写内部或外部 Flash，不使用堆
- 顶层调用、8 层固定循环栈、有限重复、永久循环、协作计时和停止
- 参数数量、类型、范围、缩进、未知名称、不支持设备和错误行号
- 点阵表情、自定义 13×7 图案、0～100 数字及关闭显示
- PC3 20 ms 去抖，SC7 倾斜缓存，CS100A 距离缓存及新鲜度检查
- 程序运行时 SC7 50 Hz、CS100A 25 Hz；待机和 USB 文本日志 10 Hz
- 停止、自然完成、异常、重新下载和复位前进入电机安全停止入口

## 实板验证结果

2026-09-23，机器人 USB 枚举为 COM123，DAPLink 为 COM92，原系统固件 `KLQ ROBOT FW 0.2` 的 `VALID=1`。通过机器人 USB `reset` 进入 `KLQ USB BOOT 0.2`，使用 `tools/klq_usb.py flash ... --run` 写入系统固件。首次镜像通过引导 CRC；实测安全演示程序在电脑串口关闭时被调试日志拖慢，因此把 USB 调试日志改成 3 ms/包超时，命令应答仍用原可靠发送路径。随后重新烧录本页所列 19,660 字节镜像。引导 `END` 对目标 Flash 计算完整镜像 CRC，并在校验成功后提交有效元数据。重新枚举后 `INFO` 返回 `KLQ ROBOT FW 0.3; VALID=1; USER_STATE=0; PY_ERROR=0; PY_LINE=0`。本轮没有通过 SWD 读回计算 SHA-256；文件 SHA-256 是本机构建镜像的值，板载完整性证据为引导 CRC 与启动时 `VALID=1`。

`python tools/test_python_runtime.py` 实板通过：

- 函数调用、功率设置、数字/表情/自定义图案/关闭显示、带小数的等待、有限重复和自然结束。
- 零次循环跳过代码、两层嵌套循环、`klq.stop()`。
- 未知函数返回 `PY_ERROR=2/PY_LINE=2`；红外与电机协议未接入时返回 `PY_ERROR=6/PY_LINE=2`。
- PC3 当前松开状态可结束 `wait_button(0)`；未操作按下状态。
- 外接传感器缺席时 `wait_distance()` 保持等待并可由 USB `STOP` 终止。
- 主机平放时 `wait_tilt(0)` 保持等待并可由 USB `STOP` 终止。
- `while True` 中的协作等待可由 USB `STOP` 终止，停止响应测试小于 0.5 秒。
- 上位机关闭 COM123 后，含两次 0.5 秒循环等待的程序按时自然完成；重新打开串口读取 `USER_STATE=2/PY_ERROR=0`。

`python tools/test_user_program.py` 实板通过：16 KiB 长度边界、错误/正确 CRC、下载与显示状态、运行/完成/停止、复位清空 RAM 程序。SC7 日志持续输出，测试期间读取到约 `ROLL=-4°、PITCH=0°`。三个外接端口均报告 `OFF`，用户确认当前没有外接传感器，因此 CS100A 连机测试本轮不适用。

自动测试结束后把 `tools/klq_python_smoke.py`（169 字节）下载到 RAM 并运行一次；等待约 3.8 秒后 `INFO` 返回 `USER_STATE=2; USER_VALID=1; UI=1; PY_ERROR=0`。该安全演示程序留在 RAM 中供再次启动，只操作点阵，不控制电机。

仍需人工观察点阵图案效果、操作 PC3 按下/释放与主机四方向倾斜。电机和红外实板动作需真实设备协议；目前 API 明确返回 `PY_ERROR=6`，不能视为动作成功。
