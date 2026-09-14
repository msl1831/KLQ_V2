# 首次 USB 成功版本：回归对照

后续主线已修复并通过实机更新与重连测试。此目录仅保留排查时的历史对照，不是当前板载版本，最终结果见 `docs/verification_20260914.md`。

此目录保存从本次开发操作记录恢复的首次成功烧录时的引导源码，用于区分后续代码变化与 USB 主机/设备状态影响。不是已经解决 USB 重连问题的版本。

`bootloader/` 恢复首次烧录之前的源码补丁及当时的编译修正。启动文件、描述符来自同一 Geehy SDK。`usbd_interrupt.c` 为上游提交 `236fbc3ed9ba4e7a8f2e617dc783fd2bd1b746e1` 的未修改文件，保留官方 suspend 中的控制器复位流程。

源码中 Geehy 声明继续适用；许可证见 `vendor/APM32E10x_EVAL_SDK-main/GEEHY COPYRIGHT NOTICE.txt`。

在项目根目录执行：

```powershell
.venv\Scripts\python.exe tools\build.py --usb-baseline
```

输出到 `build/klq_usb_baseline/`，不覆盖当前引导构建目录。镜像链接地址为 `0x08000000`，只能通过 DAPLink/SWD 写入，不能把它当作 `0x08008000` 的应用通过 USB 下载。

回归测试结果：9452 字节对照镜像已重新写入目标，Windows 仍未枚举。早期首次测试成功不等于当前已恢复。当前继续排查 Windows 设备状态和固件状态，尚不能据此认定硬件损坏。

注意：此版本的 USB_RESET 计数包含官方 suspend 流程产生的内部复位，不能单独用来证明主机是否发出过总线复位。对照版本也不包含后续可靠性修订，不建议作为正式引导发布。
