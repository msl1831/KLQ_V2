# 科乐机外接串口电机固件：第一阶段

> 历史 HK32 板记录，2026-10-26 起由 PY32 无码盘板替代。本文以下的 `MOTOR/firmware`、`MOTOR/tools` 为迁移前路径；历史源码、工具和产物现位于 `MOTOR/legacy/hk32/`。当前默认构建入口已生成 PY32 镜像。这里的 SWD 成功记录仅适用于旧 HK32 板。

目标 MCU 为用户已确认的 QFN20 `HK32F030MF4U6`。

## 已给定接线

用户提供的原理图截图已保存在 [板级电路截图](docs/board_schematic_20260924.png)，SHA-256 `4d2ea04ee18586d89878f318c5fda7a6dabba8f1c3f88ee774370a754411464a`；以下网络以截图与用户口头确认为准，元件数值和未展示的整板供电仍应以原理图源文件核对。

| MCU 引脚 | 用途 | 复用/限制 |
| --- | --- | --- |
| PA3 | 电机板 UART TX | USART1_TX，AF1 |
| PB4 | 电机板 UART RX | USART1_RX，AF1；保留 PB5 给 SWCLK |
| PB5 / PD5 | SWCLK / SWDIO | 调试与首次烧录，应用不得占用 |
| PD1 / PD2 | 双路码盘数据 1 / 2 | 输入，具体边沿、每转脉冲数待板测 |
| PD3 / PD4 | 两路 PWM 输出 | TIM1_CH3 / TIM1_CH4，AF3；分别接同一颗 DRV8837 的 IN1/IN2 |
| PD6 | 码盘红外发射驱动 | 低电平点亮；上电与引导阶段默认关闭 |

芯片只有一个 USART1，板上 UART 同时承担日常控制与升级。用户确认 PD3/PD4 分别接**同一颗 DRV8837** 的 IN1/IN2，nSLEEP 直接拉高。两根 PWM 是同一电机的方向/速度输入，不是两台独立电机。TI 真值表规定 IN1=IN2=0 为滑行停止、1/1 为刹车；具体停车方式需结合机械安全要求确定。nSLEEP 无法由 MCU 拉低，因此引导、复位和失联时必须可靠地令两个输入为低，硬件还需核对是否有明确下拉。

截图可见电机驱动 VM 经输入保护/滤波接 VCC，VCC 引脚接 3V3；SWD 接口引出 SWCLK/SWDIO，NRST 有上拉与电容；码盘红外由 3V3 经二极管/电阻到 PD6，符合低电平点亮。截图中 M1/M2 至 DRV8837 IN1/IN2 的网络上未见单独外部下拉；应量测 MCU 复位、引导擦写及掉电瞬间的两输入电平与电机动作，不能仅凭软件输出为零认定复位期间安全。

## 串口升级可行性与边界

可行方案是**自编常驻 Flash 引导**：SWD 首次烧录引导，主控以后在同一条 115200 8N1、3.3 V TTL 串口上发送 KLQ1 升级帧。电机上电先执行引导；应用镜像无效时留在引导；有效时经过短暂进入窗口后跳转应用。正常运行可用 `ENTER_BOOTLOADER(0x01F0)`，应用先停电机、关红外，再请求软件复位。主控升级期间应停止用户程序、独占外接端口并暂停设备轮询；升级结束重新做 INFO 发现。

目前没有证据证明该 M 系列提供可由 PA3/PB4 进入的片上 ROM 串口引导，也不能把普通 HK32F030 或 STM32F030 的 ROM 启动方式套用。该芯片数据手册给出 16 KiB Flash、2 KiB SRAM、128 字节页。用户提供的航顺库显示物理可编程 Flash 在 `0x08000000`，复位时映射到 `0x00000000`；该系列另有 `FLASH->INT_VEC_OFFSET` 寄存器。航顺 `system_hk32f030m.c` 使用它设置 Flash 向量偏移，并要求偏移按 0x200 对齐。因此应用向量表放在偏移 0x1200，引导跳转和应用启动均须保持该偏移；无需预留 SRAM 向量副本。Cortex-M0 跳转不能照抄主控 Cortex-M3 的 VTOR 写法。Flash 擦写和向量偏移仍需按该系列用户手册及实板验证。

### 固件分区（已由链接 map 检查）

| 范围 | 大小 | 用途 |
| --- | ---: | --- |
| `0x08000000–0x080011FF` | 4.5 KiB | 常驻串口引导；禁止串口覆盖 |
| `0x08001200–0x08003F7F` | 11648 B | 电机应用镜像 |
| `0x08003F80–0x08003FFF` | 128 B | 独立有效记录页，最后提交 |

最初按 4 KiB 构建的最小引导 BIN 已达 4084 B，只余 12 B，故将应用起点提高到下一个 0x200 对齐地址。实板修正后的引导为 4116 B，在 4.5 KiB 分区内余 492 B。若后续引导超出预算，需重新划分并同步更新升级包、链接地址和主控校验。数据手册中的 `0x00000000` 是启动别名，不能用作 Flash 编程/链接的物理地址；航顺 DFP 和芯片头文件均指向 `0x08000000`。UART 接收缓冲和校验缓冲须合计控制在 2 KiB SRAM 内。

### 升级事务草案

沿用 `UL/docs/klq_sensor_protocol.md` 的 KLQ1 帧格式、CRC-32、请求序号和响应状态；新命令编号只是本工程草案，主控接入前统一确认：

| 命令 | 草案编号 | 请求数据 | 作用 |
| --- | ---: | --- | --- |
| INFO | `0x0001` | 空 | 报 mode=0/1、device_class=2、board_id=2、hw_compat=1、最大镜像长度、UID；本芯片 UID 只有 64 位，第三字填 0 |
| BOOT_HOLD | `0x0009` | 空 | 有效应用启动前的 500 ms 窗口中留在引导 |
| ENTER_BOOTLOADER | `0x01F0` | 空 | 应用先停机再复位；引导中返回 OK |
| BEGIN | `0x0200` | board_id:u16, hw_compat:u16, length:u16, crc32:u32 | 先擦除有效记录，再擦应用页；只接受本板身份和应用区长度 |
| DATA | `0x0201` | offset:u16, bytes:2–96 | 偶数偏移、偶数长度、顺序写入并读回；相同数据重复包可重答 OK |
| END | `0x0202` | 空 | 校验栈/复位向量、整包 CRC、写入并读回有效记录 |
| RUN | `0x0203` | 空 | 仅在有效记录和镜像复查通过后跳转 |

`firmware/boot/update.c` 实现 BEGIN/DATA/END 内核；`boot_hw.c` 接入航顺 GPIO、USART1、Flash 寄存器。擦写只在应用和元数据区域，逐包读回；引导启动时独立复查有效记录与整包镜像。中断传输或断电后，因为有效记录先被擦除，复位应停留在引导供重试。单镜像槽无旧版回退，因此必须保留 SWD/NRST 实体恢复途径。有效应用上电后有 500 ms 串口窗口，可发送 `BOOT_HOLD` 留在引导；主控能否单独重启电机板尚待核实。`BEGIN` 同步擦除应用页，主机应允许最多约 10 s 响应时间。应用已实现停机、应答后软件复位的 `ENTER_BOOTLOADER`，但全部时序仍需实板验证。

实板首次擦除后，未编程页主要读为 `0x00`、少量为其它值；航顺用户手册也注明该系列擦除值不确定。引导擦除元数据页后因此只检查有效标记已经失效，并要求擦除/编程产生 EOP，不能检查整页为 `0xFF`。串口升级的实际擦写和断电恢复还需通过 UART 实板试验。

## 应用安全内核

`firmware/app/motor_safety.c` 的当前默认是：有符号 PWM 范围 -1000～1000，正值只驱动 IN1、负值只驱动 IN2，切换前先将两路归零；命令设为零、非法数值或 250 ms 内未收到新控制命令时，两路均归零且红外发射关闭。板级输出回调需把 `emitter_on=1` 映射为 PD6 **低**。250 ms 是初版失联停机预算，主机控制周期和实际刹停距离尚未验证；应用不得依赖主控单方面发 STOP 来保证停机。

应用串口命令：`INFO(0x0001)`；`SET_PWM(0x0300)` 请求为 little-endian 有符号 16 位 PWM、响应为实际请求值；`GET_STATUS(0x0301)` 返回 uptime:u32、duty:i16、PD1/PD2 当前电平位图:u8、active:u8；`STOP(0x0302)`；`ENTER_BOOTLOADER(0x01F0)`。PWM 定时器设为 20 kHz。码盘目前只读原始两路电平，尚未实现计数、转速或闭环；轮向、电气极性、每转脉冲数须实板确定。

## 本机构建与首次下载文件

从仓库根目录运行：

```powershell
& MOTOR/tools/build_boot.ps1
& MOTOR/tools/build_app.ps1
python MOTOR/tools/pack_factory.py
```

脚本使用 `F:\HK32\HK32F030Mxx系列MCU开发资料（2022-09-16更新）\3、片外设库、工程模板、参考例程\1、片外设库\01 HK32F030MF4P6_Fwlib-Template` 的 HK32F030M CMSIS 头文件、启动文件及 system 源码，ARMCC 5.06 update 6 build 750；不使用其它 F030 系列头文件。该厂商模板压缩包 SHA-256 为 `95fdb98c14925d5feb0e0134e596b850813413b837de0e7556b253fb13d1cac1`。

| 产物 | 地址/大小 | SHA-256 |
| --- | --- | --- |
| `firmware/boot/build/motor_boot.bin` | 0x08000000，4116 B | `e2cf488fbd12569858d3b9be08a8acaf9b02fcd5264848c262a40b1da14ad103` |
| `firmware/app/build/motor_app.bin` | 0x08001200，2780 B | `962fe41bca5e39f09e2acbd1db3334b3ba00db8b5e0d795eedac66f9be6d3759` |
| `firmware/factory/build/motor_factory.hex` | 稀疏地址，共 6912 B 有效内容 | `197b87dcb3d8e39d820dd1e795690191a7ae4944ae60588a577355774a300ac4` |
| `firmware/factory/build/motor_factory.bin` | 0x08000000，16384 B，间隙为占位 `0xFF` | `0beef5ddde9403364f9d2a26e1b02fe40caad5c2a074f6427bb0157acc388a5f` |

首次 SWD 下载优先使用 `motor_factory.hex`：它只指定物理 Flash 中的引导、应用和末页有效记录；`motor_factory_manifest.json` 记录板号、兼容号、应用 CRC32 `0x0DC5AB24`、长度与镜像哈希。`motor_factory.bin` 为连续地址文件，空白间隙的 `0xFF` 只是占位，芯片擦除后这些未编程地址不保证等于 `0xFF`，不能用整片 BIN 哈希判断烧录成功。单独烧录 boot+app 而未写入有效记录时，引导会认为应用无效。map 显示引导 Flash 4116/4608 B、RAM 1472/2048 B，应用 Flash 2780/11648 B、RAM 1448/2048 B。

本次主机 DAPLink 已断开，电机板独占 `LU_2022_8888` 探针，MCU UID `10050618-3D2B1829`。用户明确要求跳过原 Flash 备份；芯片原为 RDP Level 1，已从 RAM 执行厂商 RDP 解锁流程并触发整片擦除，复位后确认 Level 0。用 pyOCD 0.45.1 与航顺 DFP 1.0.7 烧录稀疏 HEX；引导 4116 B、应用 2780 B、有效记录 16 B 均逐字节读回一致。复位运行 1.5 s 后 PC=`0x08001866` 位于应用、向量偏移=`0x1200`、TIM1 CCR3/CCR4=0、PD6=1（红外关闭），随后已恢复运行。串口升级的 Flash 擦写与主机协议尚未实板验收；重新连接主机 DAPLink 时仍须按 UID 显式选探针。

航顺用户手册 V1.8 规定 RDP Level 1→0 会整片擦除。若另一块新板仍为 Level 1，可先运行 `& MOTOR/tools/build_ram_unprotect.ps1` 生成 316 B SRAM 程序，再以 `.venv/Scripts/python.exe MOTOR/tools/run_ram_unprotect.py --probe-id <探针完整UID> --uid <芯片64位UID> --pack <HK32F030M器件包路径>` 执行；该工具逐一核对 CPUID、UID 和 RDP Level 1，仅从 SRAM 修改 RDP。重置后须确认 `FLASH_OBR & 0x6 == 0` 才能烧录。直接从调试 AP 对 `FLASH_AR` 写入选项地址会使此芯片锁死，故不保留那条失败途径。

## 当前实现状态与下一步

- 已固定 IO，接入航顺寄存器级引导与应用，生成各自 BIN/HEX/map，并打包首次烧录工厂镜像。`update.c`、`motor_safety.c` 的 Windows 主机模拟测试通过（`tests/test_update.c`、`tests/test_safety.c`）。ARMCC5 编译、SWD 烧录、有效区域读回、上电跳转与无命令停机寄存器检查通过；**串口协议、实际电机动作、码盘和串口升级尚未验收**。
- 已收到板级电路截图。仍需核对完整原理图或实板，特别是 PD3/PD4 复位电平、主控能否单独重启电机板、码盘信号极性/脉冲数。
- 用户提供 `F:\HK32`，内有 HK32F030M 用户手册 V1.8、航顺外设库/示例和 Keil DFP 1.0.12 包；本机还安装 DFP 1.0.7。构建使用上述 F030M 模板源码，已检查 Flash/RAM map。后续主控串口升级器需严格按本文协议实现、复测。
- 实板按“首次 SWD 烧录 → INFO → 完整升级 → 重启 → 断电中断升级 → 错误板号/CRC → 应用失联恢复”验证；每次升级和复位都测量 PD3/PD4 电机输出与 PD6 红外状态。主控端升级入口在电机板通过上述验证后再接。

参考：[航顺 HK32F030M 数据手册](https://v4.cecdn.yun300.cn/100001_1901185243/HK32F030M%20Datasheet%20Rev.1.2.pdf)、[航顺 HK32F030M 用户手册目录](https://www.hsxp-hk.com/companyfile/1870361201793925120-20-10.html)、[TI DRV8837 数据手册](https://www.ti.com/lit/ds/symlink/drv8837.pdf)。
