# KLQ USB 引导

本工程面向 APM32E103RET6。引导仅初始化电源保持、时钟、TM1640 点阵和 USB，不初始化蓝牙、用户传感器、电机或外部 Flash。

当前已烧录的主线引导为 9272 字节。机器人系统固件的 USB 下载、校验、自动启动、返回引导及连续重连实测通过，见 [验证记录](docs/verification_20260915_ui_ram.md)。当前串口为 COM123，重新接入时编号可能改变。

## 硬件与启动

| 资源 | 配置 |
| --- | --- |
| PC5 | 在 C 运行库初始化前拉高并配置为输出，保持电源 |
| PB6 / PB7 | TM1640 CLK / DIN，软件时序，低位先发，无 ACK |
| 点阵 | 13 列 GRID1～13 从左到右，7 行 SEG1～7 从上到下；每列一个字节，bit0 为最上行 |
| PA11 / PA12 | USB D- / D+，CDC 虚拟串口 |
| 晶振 | 外部 16 MHz，PLL 得到 72 MHz，USB 分频得到 48 MHz |
| SWD | PA13 / PA14，保留 DAPLink 调试 |

进入引导显示下载箭头。系统固件下载校验通过后显示对勾，镜像校验失败显示叉号。当前最小引导不实现按键菜单和长按关机；电源保持持续有效。

上电或普通复位时，引导先校验系统固件；镜像有效则自动启动，系统固件播放开机动画并进入待机。系统固件收到 USB `reset` 后，会在 RAM 顶部写入带反码校验的一次性引导请求再软复位；引导消费并清除该请求后停留在下载界面。系统固件无效时也停留在引导。强制进入引导的实体按键方案要在确认 PC3／PC4 有效电平后补充。

## 固件分区

以下均为 MCU **内部 Flash**，只用于引导和机器人系统固件。上位机生成的 Python 用户程序不得写入这些区域，当前只驻留 RAM；外部 GD25Q80ESIG 是否用于用户程序掉电保存尚未决定。

| 区域 | 起止地址（含末地址） | 大小 |
| --- | --- | --- |
| 引导 | `0x08000000`～`0x08007FFF` | 32 KiB |
| 机器人系统固件（代码中暂称应用） | `0x08008000`～`0x0807F7FF` | 478 KiB / 489472 字节 |
| 系统固件有效记录 | `0x0807F800`～`0x0807FFFF` | 2 KiB |

USB 系统固件更新命令不接受任意写地址，只按系统固件起点加顺序偏移写入。开始更新时先擦除有效记录，再擦除所需页面；分包写入后读回检查，最后核对整个系统固件的 CRC-32 和初始栈／复位向量，通过后才提交有效标记。中途断电或传输中断后，不允许运行残缺系统固件，可以回到引导重新下载。

这是单系统固件槽方案，开始替换后不保留旧版本回滚副本。引导不通过 USB 更新自身，也不修改保护选项字节。Python 用户程序由系统固件接收、校验并存入 16 KiB RAM 缓冲，复位或断电后失效；当前尚未接入解释器。

## 编译

在本项目目录打开 PowerShell：

```powershell
.venv\Scripts\python.exe tools\build.py
.venv\Scripts\python.exe tools\build.py --demo
```

默认使用已安装的 `C:\Keil_v5\ARM\ARMCC\bin`（ARM Compiler 5.06）。换电脑时可设置环境变量 `KLQ_ARMCC` 为该编译器的 bin 目录。SDK 已放在 `vendor/APM32E10x_EVAL_SDK-main`，不依赖 Keil 器件包编译。

输出：

- `build/klq_bootloader/klq_bootloader.hex`：DAPLink 烧录引导，地址在文件中。
- `build/klq_bootloader/klq_bootloader.bin`：原始引导镜像，起点 `0x08000000`。
- `build/klq_bootloader/klq_bootloader.axf`：带调试信息的 ELF。
- `build/klq_demo_app/klq_demo_app.bin`：链接在 `0x08008000` 的系统固件测试镜像，包含 SC7A20HTR、五路 UART 初始化，以及 UART1～3 外接设备自动发现和 CS100A 服务。
- 同目录 `.map`：空间占用及符号地址。

本机 Python 工具依赖隔离在 `.venv`。其他环境可用 Python 创建虚拟环境后安装 `pyserial`、`pyocd`；协议工具只需要 `pyserial`。

## 首次通过 DAPLink 烧录

```powershell
.venv\Scripts\python.exe tools\dap_flash.py
```

脚本使用本机 Geehy APM32E1xx_DFP 1.0.3 内的官方 Flash 算法，选择探针 `LU_2022_8888`。可用 `--pack` 指定器件包路径、`--probe` 指定其他探针。写入前备份整个内部 Flash 到 `backups`，仅擦除引导镜像涉及的扇区，烧录后逐字节读回验证并复位。

仅检查引导是否与编译产物一致：

```powershell
.venv\Scripts\python.exe tools\dap_flash.py --verify-only
```

最初的板载程序备份为 `backups/original_flash_20260914.bin`，长度 524288 字节，SHA-256 为 `2e86d32315ebf7af0d2fa4efdb051de3d00f3bd85b5580a16f6c89b0ed869b18`。它是升级前的原始内部 Flash，保留用于必要时恢复。

## 通过机器人 USB 下载和通信调试

连接机器人自己的 USB 数据口，不是 DAPLink 的串口。Windows 设备管理器显示“USB 串行设备”，实际 COM 编号可能变化。

```powershell
# 列出串口和 USB 标识
.venv\Scripts\python.exe tools\klq_usb.py ports

# 不指定 --port 时自动选择唯一匹配的设备
.venv\Scripts\python.exe tools\klq_usb.py info
.venv\Scripts\python.exe tools\klq_usb.py echo "hello KLQ"

# 下载内部 Flash 中的机器人系统固件并启动
.venv\Scripts\python.exe tools\klq_usb.py flash build\klq_demo_app\klq_demo_app.bin --run

# 监视系统固件输出；自动识别 KLQ USB 并在拔插后重连
.venv\Scripts\python.exe tools\serial_monitor.py

# 单独启动有效系统固件
.venv\Scripts\python.exe tools\klq_usb.py run

# 系统固件测试镜像也支持 info、echo、display、reset；reset 返回引导
.venv\Scripts\python.exe tools\klq_usb.py info
.venv\Scripts\python.exe tools\klq_usb.py reset

# 系统固件运行时：把 Python 源码下载到 RAM，可选进入运行状态
.venv\Scripts\python.exe tools\klq_usb.py program path\to\program.py
.venv\Scripts\python.exe tools\klq_usb.py program path\to\program.py --run
.venv\Scripts\python.exe tools\klq_usb.py stop
```

多个设备时，在子命令前添加 `--port COM123` 等明确选择。`reset` 或从引导 `run` 后设备需要重新枚举，待端口出现后再执行下一个命令。系统固件崩溃时无法依赖其 USB 命令；普通硬件复位会再次自动启动有效固件，目前需通过 DAPLink 恢复，实体按键强制进入引导功能尚待加入。

修复后的断开流程先关闭 USB 收发器和外设时钟，再把 D+/D− 同时拉低 300 ms；初始化完成后恢复输入模式。延时使用 SysTick，不依赖调试器管理的 DWT/DEMCR，避免 SWD 调试会话结束后计时停住。每次跳转或复位后的枚举需要数秒，立即运行下一条 CLI 命令可能暂时找不到串口。

USB 使用带 CRC 的二进制协议。普通串口终端输入文字不会执行命令；`echo` 是通信诊断。当前系统固件测试镜像经同一 USB CDC 约每 100 ms 输出一行 SC7A20HTR 三轴及 Roll/Pitch 信息，串口监视器可直接查看；协议客户端会从文本流中重新同步二进制帧。

**源码断点、单步和寄存器查看仍由 DAPLink/SWD 提供**，本引导没有实现 USB GDB 调试代理。可用 pyOCD 启动 GDB 服务，再用支持 Cortex-M 的 GDB 加载 `.axf` 符号：

```powershell
.venv\Scripts\python.exe -m pyocd gdbserver --pack C:\Users\Administrator\AppData\Local\Arm\Packs\Geehy\APM32E1xx_DFP\1.0.3 -t apm32e103re -u LU_2022_8888 -f 1000000
```

GDB 连接 `localhost:3333`。也可在 Keil 中建立 APM32E103RE 工程并选择 CMSIS-DAP。调试系统固件时应使用链接在 `0x08008000` 的 `.axf`，不要用错误链接地址的镜像覆盖引导。

## 后续机器人系统固件的要求

1. 将链接起点设置为 `0x08008000`，最大长度为 `0x77800`。
2. 向量表放在系统固件起点；设置 `SCB->VTOR = 0x08008000`。
3. 初始 MSP 应在 `0x20000000`～`0x20020000` 的 RAM 范围内且 8 字节对齐；复位向量应为镜像范围内的 Thumb 入口。
4. 初始化时继续保持 PC5 高电平，不能整组复位 GPIOC 导致电源关闭。
5. 重新初始化系统固件使用的时钟、SysTick 和 USB。引导跳转前关闭 USB 与 SysTick、清除 NVIC 使能与待处理中断，设置 VTOR/MSP 后进入系统固件复位处理程序。
6. 运行中通过 USB 返回引导时，系统固件必须先调用 `board_request_bootloader()` 再复位。引导在系统固件运行时不作为后台服务执行。
7. Python 用户程序只能放入 RAM 缓冲区，不得写入系统固件区或元数据页。相同 BEGIN／DATA／END 命令在引导中表示系统固件升级，在系统固件中表示 RAM 用户程序下载；主机必须先用 INFO 识别当前环境。

## 协议与文件结构

协议头 16 字节，小端：`"KLQ1" | seq:u32 | cmd:u16 | length:u16 | crc32:u32`，之后是 payload。CRC 为常见反射 CRC-32（与 `zlib.crc32` 相同），覆盖头部前 12 字节及 payload。最大请求 payload 为 1028 字节。

响应命令号为请求号 OR `0x8000`，payload 前 4 字节为状态码，其后为返回内容。主机按请求／应答串行发送；丢失应答时可重发相同序号与完整请求。设备只缓存上一条有效请求的应答，不支持断线后续传。

| 命令 | 编号 | 请求内容 |
| --- | --- | --- |
| INFO | 1 | 空，返回设备、分区和系统固件有效状态 |
| BEGIN | 2 | 长度 u32、整包 CRC u32；引导中开始系统固件擦除，系统固件中开始 RAM 用户程序接收 |
| DATA | 3 | 顺序偏移 u32、1～1024 字节数据；写入当前环境对应目标 |
| END | 4 | 空；引导中提交系统固件有效记录，系统固件中校验 RAM 程序并显示完成图标 |
| RUN | 5 | 空；引导中跳转有效系统固件，系统固件中启动有效 RAM 用户程序状态 |
| ECHO | 6 | 原样返回 payload |
| RESET | 7 | 空，写入一次性请求并软复位，停留在引导 |
| DISPLAY | 8 | 13 个列字节，bit7 忽略；系统固件只在用户程序运行状态接受 |
| STOP | 9 | 空；系统固件停止用户程序并恢复待机 |
| FINISH | 10 | 空；系统固件标记用户程序正常结束并恢复待机 |

状态码：0 成功，1 未知命令，2 长度错误，3 通信 CRC 错误，4 状态错误，5 越界／偏移错误，6 Flash 错误，7 系统固件镜像无效。

核心源码：`board.c` 为电源／时钟／一次性引导请求／跳转，`tm1640.c` 为点阵，`usb_serial.c` 为 CDC 适配，`protocol.c` 为协议分发及引导侧 Flash 管理。系统固件镜像通过 `KLQ_DEMO_APP` 使用同一组底层驱动，并增加 `robot_ui.c`、`user_program.c`、`sc7a20.c` 和 `peripherals.c`；它的 BEGIN／DATA／END 只操作 RAM，不包含 Flash 擦写路径。

USB 当前沿用极海示例的 `VID:PID = 314B:0108` 和演示序列号，仅用于本开发板联调，不代表 KLQ 产品已分配 USB 标识。量产标识及唯一序列号另行设置。

## 来源与修改

- [Geehy 官方 APM32E10x EVAL SDK](https://github.com/GeehySemi/APM32E10x_EVAL_SDK)：USB Device 库、标准外设驱动、CMSIS 与启动文件。保留 SDK 原始许可证。
- [APM32E103xCxE 数据手册](https://geehy.com/uploads/tool/APM32E103xCxE%20datasheet%20V1.9.pdf)：器件容量与时钟约束；Flash 页大小同时由官方 FLM 及连接目标核实。
- TM1640 映射来自用户提供的 7×13 LED 原理图，通信格式参考 Titan TM1640 数据手册。

SDK 改动：`usbd_interrupt.c` 的休眠处理移除等待 USB RESET 的阻塞循环，保留非低功耗工作；其余库文件保持原样。描述符和启动文件的项目副本保留 Geehy 声明，描述符产品名改为 KLQ USB Boot。新增代码与第三方代码的授权分别适用，未给整个项目套用其他开源许可证。

## 本机排障脚本

`tools/refresh_klq_usb.ps1` 用于在管理员权限下移除已离线的 KLQ 实例并扫描设备，不删除驱动。`tools/klq_hub_power.ps1` 仅针对本次观测到的一个特定外置集线器，临时关闭其自动省电并保存原值；加 `-Restore` 可恢复。它会重启该集线器，使用前应确认没有其他正在工作的设备连接其下。这些脚本不是 USB 下载的前置步骤，也不应直接用于其他电脑。

本次测试已执行恢复操作，集线器省电设置最终保持原值 `Enable=True`。正常使用本引导不需要修改电脑电源策略。
