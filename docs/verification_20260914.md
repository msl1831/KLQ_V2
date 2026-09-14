# KLQ 最小引导验证记录（2026-09-14）

## 最终状态

APM32E103RET6 板子已运行修复后的主线引导，停留在引导模式并显示下载图标。机器人自身 USB 枚举为 COM123（VID:PID 314B:0108）；DAPLink 串口 COM92 是另一个设备。内部应用区安装了测试应用，未实现 Python 解释器或机器人业务功能。

| 镜像 | 长度 | 内部 Flash 起点 |
| --- | --- | --- |
| `build/klq_bootloader/klq_bootloader.bin` | 9472 字节 | `0x08000000` |
| `build/klq_demo_app/klq_demo_app.bin` | 8616 字节 | `0x08008000` |

引导 SHA-256：`4227fde68a94a0b9642ec1db4f40b7b2c70e66b7891fedcd4ca430521ac60d9f`。在 USB 更新测试结束后通过 DAPLink 逐字节读回，确认引导与编译镜像一致。

## 实测通过

- ARMCC 构建引导和测试应用，无编译警告。
- DAPLink 烧录、备份和读回校验；烧录脚本加入复位暂停后，完整重写流程已成功执行。
- INFO/ECHO，以及 26 次包含 0、63、64、65、1024、1028 字节等边界的回传。
- 相同请求的重复应答缓存、传输 CRC 错误拒绝、应用长度越界拒绝、错误后的继续通信。
- 只下载首包后复位，残缺镜像不能运行；可以重新下载。
- 整包 CRC 错误镜像不能提交或运行。
- 完整下载 8616 字节应用、整包校验、运行应用、应用 USB 回传、复位返回引导。
- 主动将 DWT 控制寄存器与 DEMCR 清零并结束 SWD 会话后，仍能通过上述完整更新流程。
- 恢复外置集线器原有省电设置后，连续 5 轮“启动应用 → 回传 → 复位返回引导”全部通过。
- 引导点阵的图标和方向此前由用户观察实物确认；最终版本未修改图案或映射。

主要命令：

```powershell
.venv\Scripts\python.exe tools\build.py
.venv\Scripts\python.exe tools\build.py --demo
.venv\Scripts\python.exe tools\test_protocol.py
.venv\Scripts\python.exe tools\test_protocol.py --port COM123
.venv\Scripts\python.exe -u tools\test_update.py build\klq_demo_app\klq_demo_app.bin --allow-app-erase
.venv\Scripts\python.exe tools\dap_flash.py --verify-only
```

## 修复及排查结论

1. USB 软件断开原先只对控制器复位并处理 D+，曾观测到跳转应用后主机仍保留旧串口、应用地址寄存器已经回到零。现在先关闭收发器/USB 时钟，再同时拉低 D+/D− 300 ms，重新初始化后释放数据线。
2. 延时原先依赖 DWT CYCCNT。检查本机 pyOCD 源码确认其 Cortex-M 断开流程可能清除 DEMCR，关闭 DWT 计时；曾观测到初始化延时阶段没有完成。现改为 SysTick 向下计数器累积时间，独立于调试计数器，并完成显式关闭 DWT 的回归验证。
3. Windows 曾保留离线设备实例，外置集线器也处于选择性挂起状态。最初的普通权限 PnPUtil 清理被拒绝；之后经系统管理员授权成功清理并重启该集线器，USB 曾恢复。期间临时关闭该集线器省电做对照，最终已经恢复原值并验证重连通过。不能把集线器省电单独认定为全部故障的根因，也没有证据据此认定机器人硬件损坏。

权限日志、原始电源设置和板载固件备份保存在本机 `build/`、`backups/`，未上传仓库。记录中早期失败的版本与阶段结果见 [历史联调记录](bringup_20260914.md)。

## 验证范围

最终版本尚未再做整板完全断电的冷启动实测；中断更新测试采用软件复位，不等同于写入时真实断电。未测试全部 478 KiB 应用区写满、其他电脑/集线器兼容性或长期压力运行。USB 支持固件下载和通信诊断；源码断点、单步仍通过 DAPLink/SWD。
