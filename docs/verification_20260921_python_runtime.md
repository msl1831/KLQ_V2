# KLQ 精简 Python 首版验证记录

日期：2026-09-21

## 构建结果

- 系统固件：`build/klq_demo_app/klq_demo_app.bin`
- 链接地址：`0x08008000`
- 文件长度：19,644 字节
- SHA-256：`60C9DC6EC59E1A90EB0E40B391128B74008D8C4062C1B0D6BCB846F6F51A12F1`
- map：RO 19,380 字节，RW 26,424 字节，ROM 19,644 字节
- 引导回归：`klq_bootloader.bin` 9,272 字节；`mini_python.c` 与 `klq_runtime.c` 未链接进 32 KiB 引导
- Python 主机工具通过 AST 语法检查，`git diff --check` 无空白错误

## 已实现并由构建覆盖

- 16 KiB RAM 源码直接解释，不写内部或外部 Flash，不使用堆
- 顶层调用、8 层固定循环栈、有限重复、永久循环、协作计时和停止
- 参数数量、类型、范围、缩进、未知名称、不支持设备和错误行号
- 点阵表情、自定义 13×7 图案、0～100 数字及关闭显示
- PC3 20 ms 去抖，SC7 倾斜缓存，CS100A 距离缓存及新鲜度检查
- 程序运行时 SC7 50 Hz、CS100A 25 Hz；待机和 USB 文本日志 10 Hz
- 停止、自然完成、异常、重新下载和复位前进入电机安全停止入口

## 实板验证状态

本次构建完成时 Windows 只枚举 DAPLink 的 COM92/HID 和 G: 盘，没有枚举机器人自身 USB CDC。pyOCD 对 DAPLink 的只读连接没有返回；另一个工作目录仍有使用 COM92 的串口监视进程，因此没有在无法确认 SWD 目标板的情况下写入 G: 盘。系统固件尚未烧录，`tools/test_python_runtime.py` 尚未在板上执行。

恢复实板连接后按以下顺序验收：

1. 确认 DAPLink SWD 当前连接的是 KLQ APM32E103RET6，并关闭占用该 DAPLink 的其他调试任务。
2. 仅把系统固件写入 `0x08008000`，保留 `0x08000000～0x08007FFF` 引导区，并读回核对 SHA-256。
3. 确认机器人 USB 枚举为 `KLQ ROBOT FW 0.3`。
4. 运行 `python tools/test_python_runtime.py`，验证调用、定时、循环、错误行、不支持设备和无限循环停止。
5. 运行 `python tools/test_user_program.py`，回归 RAM 下载、CRC、显示状态和复位清空。
6. 人工验证 PC3 按下/释放和 SC7 四方向；接入 CS100A 后验证距离条件。

电机和红外的实板功能仍需真实设备协议，当前测试应确认它们返回 `PY_ERROR=6`，不得当作动作成功。
