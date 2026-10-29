# 科乐机外接串口电机固件

2026-10-29：与超声波/红外共用的引导和首次安装包现统一从 [sensor_boot](../sensor_boot/README.md) 构建，增加 GET_LAYOUT 和应用内部板型/ABI 检查，应用 capability=9。主控USB升级转发已接入，进入引导前先确认STOP，再自动HOLD。以下独立电机引导、旧哈希和 HK32 历史材料保留，不能替代共用引导的新镜像验证记录。

本板已首次烧录共用引导1.0.1和Motor应用2.0.0，整片24 KiB读回与工厂镜像一致。已验证复位跳应用、VTOR/SysTick正常推进、无命令状态PWM为零及PA0/PA1读低；检查后恢复运行。UART升级、实际电机动作与250 ms失联停机仍待板测，详见 [首次烧录记录](docs/flash_20261029.md)。

后续生成可升级电机固件的入口为：

```powershell
& F:/work/KLQ_V2/Codex/sensor_boot/tools/build.ps1 -Device motor
```

首次安装选择 `sensor_boot/build/motor/release/motor_factory.hex`，后续升级选择同目录 `motor_update.klq`。本次专项核对修正了 Motor 应用在 ACK/TX 超时后仍复位的问题，以及 PA0/PA1 初始化的内部上拉；完整核对、当前镜像哈希及验证范围见 [boot_integration_check_20261029.md](docs/boot_integration_check_20261029.md)。下文的独立构建命令和 2026-10-26 镜像哈希仅供历史版本参考。

2026-10-26历史记录：目标改为 `PY32F002BF15U6TR` 并取消码盘，彼时完成PY32镜像编译和主机模拟，尚未烧录。旧 HK32 版本及原有烧录证据保存在 [legacy/hk32](legacy/hk32/README.md)，其板测结论不适用于新板。

## 当前 IO 与功能

来源：[motor_io分配.xlsx](motor_io分配.xlsx)，`Sheet1!A1:B7`，SHA-256 `4dbc8d4754d40aad11e8ceafbbc4769e00bf055bb2446661995389b2576985d9`。

| 引脚 | 功能 | 固件配置 |
| --- | --- | --- |
| PA0 | DRV8837 IN1 | TIM1_CH1，AF2，20 kHz PWM |
| PA1 | DRV8837 IN2 | TIM1_CH2，AF2，20 kHz PWM |
| PA3 | UART TX | USART1，AF1，115200 8N1 |
| PA4 | UART RX | USART1，AF1，115200 8N1 |
| PA2 | SWCLK | 保留 SWD |
| PB6 | SWDIO | 保留 SWD |

已按普冉 [PY32F002B Datasheet V1.8](https://www.puyasemi.com/download_path/%E6%95%B0%E6%8D%AE%E6%89%8B%E5%86%8C/MCU/PY32F002B_Datasheet_V1.8.pdf)和本地固定 SDK 核对 UART/PWM 复用。该器件为 Cortex-M0+、24 KiB Flash、3 KiB SRAM，支持 VTOR；不再使用 HK32 的 `INT_VEC_OFFSET`、USART ISR/RDR/TDR 或半字编程流程。

按此前确认继续使用同一颗 DRV8837 的两输入控制，nSLEEP 直接拉高；新 IO 表未分配 nSLEEP 控制脚。新板没有码盘、红外发射或指示灯定义。仅支持方向和 PWM 占空比控制，不提供实测转速、圈数、位置或闭环调速。

## 电机控制与安全

PWM 命令范围 `-1000..1000`：正值仅 IN1 输出 PWM，负值仅 IN2 输出 PWM；实际轮向待板测。每次改变输出前先锁存两路为零，再在下一个 PWM 周期加载新值。上电、引导、升级、STOP、非法 PWM 数值和 UART 硬件接收错误均使 IN1/IN2 为低。DRV8837 两输入低为滑行停机，不能视作主动刹车。

最后一次合法 SET_PWM 后经过 **250 ms** 未刷新，应用主循环将输出归零。主机建议每 100 ms 或更快刷新；INFO/GET_STATUS 不续期。尚未启用独立看门狗，主循环或异常处理卡死后的停机不在这项软件超时保证内。复位前 GPIO 尚未初始化时的真实电平及电机停止距离需要在新板测量。

## 分区与串口升级

| 物理范围 | 字节数 | 用途 |
| --- | ---: | --- |
| `0x08000000..0x080017FF` | 6144 | 常驻引导 |
| `0x08001800..0x08005F7F` | 18304 | 电机应用 |
| `0x08005F80..0x08005FFF` | 128 | 有效记录页 |
| `0x20000000..0x20000BFF` | 3072 | SRAM，各镜像独立分配 |

保留 KLQ1 的 16 B 帧头、CRC32、序号和状态格式，详见 [UL 公共帧定义](../UL/docs/klq_sensor_protocol.md)。身份为 `device_class=2, board_id=2, hw_compat=2, hw_revision=2`，固件版本 `0x00020000`（2.0.0）。旧 HK32 使用 `hw_compat=1`，BEGIN 会拒绝它；应用地址和工厂镜像也已改变。主控电机业务/升级器尚未接入。

| 命令 | 请求数据 | 响应/行为 |
| --- | --- | --- |
| INFO `0x0001` | 空 | 36 B 身份数据；mode=0 引导/1 应用；96 位 UID；最大应用 18304 B；应用 capability=1（PWM，无 encoder 位），引导 capability=8 |
| BOOT_HOLD `0x0009` | 空 | 仅引导：留在引导 |
| ENTER_BOOTLOADER `0x01F0` | 空 | 应用停机、应答后软件复位；引导中等同 HOLD |
| SET_PWM `0x0300` | signed PWM:i16 | 仅应用：实际请求值 i16；小端序 |
| GET_STATUS `0x0301` | 空 | 仅应用：uptime:u32、duty:i16、reserved:u8=0、active:u8；保留 8 B 格式，无码盘数据 |
| STOP `0x0302` | 空 | 仅应用：两路归零 |
| BEGIN `0x0200` | board:u16, hw:u16, length:u16, crc32:u32 | 仅引导：先擦有效记录，再擦应用所需页；响应 next_offset:u16 |
| DATA `0x0201` | offset:u16, bytes | 仅引导：offset 必须 128 B 对齐，每包 128 B；最后一包允许不足 128 B 的偶数字节；响应 next_offset:u16 |
| END `0x0202` | 空 | 校验初始栈/复位向量、整包 CRC，最后写有效记录 |
| RUN `0x0203` | 空 | 仅通过镜像复查后跳应用 |

有效应用上电有 500 ms 引导窗口；进入引导后应由主机及时发送 BOOT_HOLD。INFO 本身不延长该窗口。BEGIN 主机响应超时预算建议 10 s，DATA 请求与应答逐包进行，不连续灌入。

PY32 Flash 按 128 B 整页编程，每页只编程一次，最后一页剩余填 `0xFF`。完全相同的 DATA 重发只读回比较，不再次写页；写入/读回/提交失败或整包 CRC、向量错误后必须重发 BEGIN，从擦除重新开始。有效记录独占整页并在镜像校验后写入；中断升级不会留下可启动的旧有效标记。启动还会独立检查身份、长度、8 字节对齐的 SRAM 栈、Thumb 复位入口和整包 CRC。单应用槽无旧版回退，保留 SWD 恢复入口。

Flash 实现依据固定 SDK 的 `py32f002b_hal_flash.c/.h`：从工厂参数 `0x1FFF011C` 装载 24 MHz HSI 擦写时序，页编程第 32 个字前置 PGSTRT，检查 EOP/错误标志并整页读回；串口路径只允许应用和有效记录区域，不改选项字节。

## 构建与产物

从仓库根目录运行，工具为 ARMCC 5.06 update 6 build 750：

```powershell
& MOTOR/tools/build_boot.ps1
& MOTOR/tools/build_app.ps1
& .venv/Scripts/python.exe MOTOR/tools/pack_factory.py
& MOTOR/tools/test_motor.ps1
```

当前默认 SDK 借用 `UL/firmware/vendor/PY32F002B_Firmware`，固定提交 `394627d99085c5eae72142503b0405b84428bb20`。两个构建入口支持 `-SdkRoot` 和 `-ArmccBin`；编译器也可由 `KLQ_ARMCC` 指定。启动文件栈 1024 B、堆 0；以 `SWD_DELAY` 关闭厂商仅为复用 SWD 设计的 200 ms 延时，SWD 引脚仍保留。应用在启用中断前将 VTOR 设为 `0x08001800`。

| 镜像 | BIN/ROM | RO 执行区 | RAM（含固定栈） | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| 引导 | 4260 B / 6144 B | 4240 B | 1600 B / 3072 B | `71915f2762bbe0557552d349545fa99c96b0fd1a0f951c3869cabd7b3b4a75ea` |
| 应用 | 2840 B / 18304 B | 2820 B | 1448 B / 3072 B | `93120b3c151c856482f62e7cb6a4f51152c4c79a49ad7d5bc678197ac74a57ad` |

首次 SWD 文件：[motor_factory.hex](firmware/factory/build/motor_factory.hex)，有效数据 7116 B，SHA-256 `1b7750600910832db06dda6cfa2d44ada36e0fd37d168c26888e22c7502b6077`。连续 BIN 为 24576 B，SHA-256 `49877db81c7a0169d237a8bef42d777d558f5b31e44b1669e3c48a2631c5cdb9`。两种工厂文件均包含引导、应用、末页 16 B 记录；应用 CRC32 `0x9E7CD060`。独立 app 文件不含有效记录，首次烧录应选工厂 HEX。构建产物保持忽略，可由相同输入重建。

`test_motor.ps1` 使用本机 VC98 编译并执行实际 C 内核；`tests/host/stdint.h` 仅为其缺失的标准整数头提供适配，不进入 MCU 构建。覆盖换向、PWM 边界、零输出、250 ms 边界和计时回绕；升级覆盖错板/旧兼容号/超长/奇数长度、整页限制、乱序、重复 DATA/BEGIN/END、IO/提交失败重启、CRC/栈错误及失效状态恢复。

## 验证边界与下一步

当前通过：ARM镜像编译、链接分区检查、主机安全/升级内核测试、工厂镜像检查，以及新板首次SWD烧录/独立回读、默认启动与停机状态检查。历史 HK32 UID、RDP 解锁与 PWM 寄存器读数保留在历史目录，不能用于 PY32 身份或板测。

下一步接UART后验证INFO、低占空比正反转、STOP/失联250 ms、UART异常、完整串口升级、升级断电恢复及错误兼容号/CRC。测量PWM频率、占空比、复位电平和实际停机状态；码盘相关验收已取消。
