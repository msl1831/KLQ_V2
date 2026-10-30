> 2026-10-30暂停检查点：板上APP0.14、BOOT0.5；APP0.15已构建未装，KLQ2手机可见、PC BLE连接及下载待继续。准确当前状态见[暂停交接](bluetooth_paused_20261030.md)。

# KLQ2电脑BLE模拟客户端

`klq_ble.py`直接调用本机BLE GATT，不使用蓝牙SPP串口，也不会打开主机USB COM口。测试电脑为Windows10 Pro19045、Intel蓝牙8087:0032，Radio=On。隔离依赖已放build/bluetooth_pc_20261030/deps，未改全局Python；当前已有pyserial3.5的D:/Python/Python312/python.exe可运行。

Bleak0.22.3固定支持Python3.8..<3.14，Windows/Python3.12用WinRT2.3.0。版本固定见tools/requirements_ble.txt；其他环境建议建立venv后pip install -r tools/requirements_ble.txt。当前构建环境的WinRT依赖仅供这个本机版本，不代表Android/iOS移动APP依赖。Bleak的最新文档存在Windows10/11支持边界变化，本项目不在本轮盲升版本。

## 使用

```powershell
python tools/klq_ble.py --deps build/bluetooth_pc_20261030/deps scan
python tools/klq_ble.py --deps build/bluetooth_pc_20261030/deps --address AA:BB:CC:DD:EE:FF services
python tools/klq_ble.py --deps build/bluetooth_pc_20261030/deps --address AA:BB:CC:DD:EE:FF info
python tools/klq_ble.py --deps build/bluetooth_pc_20261030/deps --address AA:BB:CC:DD:EE:FF echo test
python tools/klq_ble.py --deps build/bluetooth_pc_20261030/deps --address AA:BB:CC:DD:EE:FF program examples/ble_display_demo.py --run
```

不指定地址时只选择扫描名精确为KLQ2且只有一个的设备；多个或未发现则拒绝连接。地址必须由本机扫描或模块MAC确认，不能照抄示例。`services`连接后列出GATT/MTU；`status`读主机0x56 ABI1，不是写模块AT。首次扫描10秒正常完成但未收到广播，未连接过任何设备；后续实板结果由主任务另记录。

ECB02C服务FFF0，FFF1通知/读、FFF2写。连接后必须先订阅FFF1，再发KLQ1帧。客户端按特征属性优先write-with-response，否则write-without-response；每次最多20B，片间20ms。DATA默认256B，可设置1..1024，固件写入块必须偶数2..1024。默认10s包含一帧的发送和等待应答，不能把USB250ms直接用于BLE。

通知只是字节流，可能拆成任意片或包含多帧；按magic/长度/CRC/sequence/response-cmd组帧。只有一个未完成RPC。完整帧发送后应答超时最多同seq重试2次；半帧写超时不盲重发，应重连并确认状态。断线清解析器、回复和会话；旧连接迟到回调不能污染新连接。CRC是传输完整性，不代表设备认证。

## APP与BOOT

`program`先本地检查1..16384B，再INFO确认APP才发送BEGIN/DATA/END。`--run`等待UI待机与USER_VALID1后RUN。普通客户端关闭不自动STOP，程序离线自主运行；需要停止请明确使用stop。

`flash`只接受已进入BOOT的设备和合法APP_BASE0x08008000镜像，先验证本地向量/长度再INFO；不会自动从APP切BOOT，更不会写引导本身。先明确reset、重连info确认BOOT，再flash。当前物理主机如果仍为驻留旧BOOT0.3，不能因为此工具有flash命令就认为蓝牙升级已经可用；必须安装并验证支持BLE的引导。

RUN/RESET不自动重试：UART TC并不能证明手机收到动作ACK；若ACK丢失，先重连INFO区分BOOT/APP和用户状态，避免BOOT RUN已经启动APP后再RUN意外启动用户源码。flash --run同样遵守这一规则。

## 验证

`python tools/test_klq_ble.py`为21项fake GATT测试，不扫描、不连接、不打开串口。覆盖20B拆包、错CRC/错seq/噪声、请求序列幂等重试、半帧写超时边界、畸形状态、并发单RPC、掉线和旧会话回调、APP/BOOT互斥、本地镜像/长度/块对齐、1280B以上下载及RUN无盲重放。日志和源hash见build/bluetooth_pc_20261030/report.json。此证明不能替代真实空中通信/模块透传/固件升级和断连验收。

参考：[Bleak Windows后端](https://bleak.readthedocs.io/en/latest/backends/windows.html)、[固定版本元数据](https://pypi.org/pypi/bleak/0.22.3/json)、[Microsoft GATT说明](https://learn.microsoft.com/en-us/windows/apps/develop/devices-sensors/bluetooth-dev-faq)。
