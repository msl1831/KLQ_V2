# IR 统一引导接入核对（2026-10-29）

结论：现有 IR 适配符合共享工作区的统一引导约束，未发现需修正的源码缺口。本轮只新增此核对记录，重新构建 IR 统一交付并运行软件验证；未改 IR 固件/共用引导/主控转发或公共 handoff，未访问真实串口、探针或板卡，未烧录、切分支、提交或推送。

工作区 `F:/work/KLQ_V2/Codex`，实际分支/HEAD 为 `main@63d1ea38f69fbd478f92751855f238340b6fa853` 加既有未提交内容，未合并 `codex/python-runtime`。开始前已核对 `docs/handoff_current.md` 第14/15节、`docs/development_setup.md` 第8/9节及现有 IR/引导源码。工具沿用 ARMCC 5.06 update 6 build 750；Puya SDK 为 `394627d99085c5eae72142503b0405b84428bb20`，本轮核对其工作区干净。

## 今后的可升级交付入口

在仓库根目录运行：

```powershell
& F:/work/KLQ_V2/Codex/sensor_boot/tools/build.ps1 -Device ir
```

该入口构建 IR 板型引导，内部调用 `IR/tools/build_sensor.ps1 -Bootloader -Configuration Release -OutputDir sensor_boot/build/ir/app`，随后运行 `pack.py --device ir`。它将身份源与共用 scatter 一起纳入应用，最终生成：

| 用途 | 文件 |
| --- | --- |
| 首次 SWD 安装，包含引导、应用、有效记录 | `sensor_boot/build/ir/release/ir_factory.hex` |
| 后续直连 UART / 经主控 USB 转发升级 | `sensor_boot/build/ir/release/ir_update.klq` |
| 工厂连续 BIN、SHA-256 清单、四文件 ZIP | 同目录 `ir_factory.bin / ir_manifest.json / ir_release.zip` |

`IR/tools/build_sensor.ps1` 不带 `-Bootloader` 时仍生成从 `0x08000000` 启动的独立应用，保持旧 bring-up 用法。当前旧独立 BIN 为6024 B、Reset=`0x0800019D`、没有 KID1，不是统一升级应用。直接使用 `-Bootloader` 只构建应用，也不替代统一入口的引导/工厂镜像/容器打包。

`IR/README.md`、`IR/docs/protocol_v1.md` 中“无升级能力”和起点0的原说明对应独立构建；可升级交付以本记录及 [统一协议](../../sensor_boot/docs/protocol_v1.md)、[USB转发说明](../../sensor_boot/docs/usb_bridge_v1.md) 为准。没有把旧20 KiB应用链接限制继续当作统一分区；目标 MCU 为24 KiB Flash /3 KiB RAM。

经主控升级的命令示例仅供后续授权实测，本轮没有执行。COM号及1～3端口必须按实际连接选择，端口不固定传感器类型：

```powershell
& F:/work/KLQ_V2/Codex/.venv/Scripts/python.exe F:/work/KLQ_V2/Codex/sensor_boot/tools/update.py --via-host --port COMx --sensor-port 2 --image F:/work/KLQ_V2/Codex/sensor_boot/build/ir/release/ir_update.klq
```

主控需运行具有 `EXT_UPGRADE=1` 的应用0.3，外接IR需已安装本板工厂引导。普通应用复位进入引导后只有500 ms握手窗口，由主控自动HOLD；没有RAM令牌。没有有效记录或CRC不符时保持引导。首次安装仍需SWD，工厂BIN/HEX不能作为UART应用数据发送。

## 源码与生成镜像核对

| 核对项 | 当前实现及证据 |
| --- | --- |
| Flash布局 | boot `0x08000000..0x080017FF`；APP `0x08001800..0x08005F7F`，容量18304 B；记录页 `0x08005F80..0x08005FFF` 独占。未改选项字节。 |
| 向量/身份 | `sensor_boot/linker/app.sct` 固定向量192 B、KID1位于`0x080018C0`且16 B、代码自`0x080018D0`开始；实际map与BIN一致。 |
| 编译身份 | `IR/tools/build_sensor.ps1` 的引导模式启用`KLQ_BOOT_APP / SWD_DELAY / SENSOR_IR`，加入`app_identity.c`和共用scatter。KID1实际解码为`board=3,hw=1,base=0x08001800,ABI=1`，处于完整应用CRC覆盖中。 |
| INFO | `device_service.c` 保持class=1、board=3、hw=1、sensor_type=2、版本1.0.0；引导构建capability=15、max_image=18304；独立构建仍为7/0。 |
| 中断入口顺序 | `main.c`先调用`sensor_app_enter()`再`board_init()`；入口在Vendor SystemInit之后关IRQ、设置VTOR=`0x08001800`、DSB/ISB后开IRQ。生成AXF的独立复核与该顺序一致。 |
| 进入引导 | 引导构建接受`ENTER_BOOTLOADER 0x01F0`。只有ACK成功入TX队列后才立即关闭发射并置`reset_pending`；主循环停止推进采样/普通命令；TX排空且至少2 ms后软件复位，等待期间保持关灯。 |
| 原有采样/安全 | PB1=左ADC_IN0、PB0=右ADC_IN7；PB3/PB2低有效发射。初始化预装高电平熄灭，故障/超时/复位仍关灯；未触碰PA2/PB6 SWD。双路差分、原始/0～1000输出及主控类型区分保留。 |

## 本轮重建与产物检查

统一构建命令通过，无ARMCC警告/错误。链接map：

| IR镜像 | BIN/ROM | RO | RAM（含固定1024 B栈、堆0） | 链接上限 |
| --- | ---: | ---: | ---: | --- |
| 引导 | 4468 B | 4448 B | 1600 B | Flash6144 B / RAM3072 B |
| 应用 | 6052 B | 5980 B | 2040 B | Flash18304 B / RAM3072 B |

应用有效范围 `0x08001800..0x08002FA3`；向量SP=`0x200007F8`、Reset=`0x080019AD`，8字节栈对齐且入口为应用范围内Thumb地址。工厂HEX只包含引导4468 B、应用6052 B及16 B有效记录，共10536 B；没有跨入选项字节。连续工厂BIN为24576 B，其它区域为FF，记录页余下112 B为FF。

实际记录为`board=3,hw=1,length=6052,reserved=0,CRC32=0x3CB2BDEC,magic=0x314D4C4B`。升级容器6072 B，KSU1头后的6052 B与应用BIN完全相同。已独立验证全部HEX记录校验和/地址/与BIN一致性、KID1、容器长度/CRC、元数据、manifest哈希及ZIP内四份文件。将应用内部board改错、或尝试打包旧独立BIN，打包器均拒绝。

| 文件 | SHA-256 |
| --- | --- |
| `sensor_boot/build/ir/boot/sensor_boot.bin` | `ec24c019152e8166f52c414564820cea05ba723e2aec8ba1f780812c7fe8acd5` |
| `sensor_boot/build/ir/app/ir_sensor_app.bin` | `19b7df54061df7583753b662352a77c93dd4d7bc6c8f0bf53ea2d22a6a653c54` |
| `ir_factory.hex` | `62c9700f3c6e6c90c8efd973a3900798ed6559442a3693849e4ecd378c81744e` |
| `ir_factory.bin` | `e75f389388ad1f7e3950477f944571a2aafd3b9c2c97d7b1a4e06cc8d49c82a1` |
| `ir_update.klq` | `f1d90156e14801f8336de4ae5c7bcd42a042446c8bb3e3e5acbba75f70dcfafb` |
| `ir_release.zip` | `aa282e4399b5e34ec7601d5b2d351fe8146323c3c508a53f218257383ac94c49` |

## 本轮软件验证与限制

- 将既有`sensor_boot/tests/test_app_entry.c`以`KLQ_BOOT_APP / SENSOR_IR`编译，链接实际IR `device_service.c / frame_codec.c`，在`IR/tests/build/boot_integration_check_20261029/entry_IR.exe`执行通过。覆盖capability15/容量18304、错误长度拒绝、ACK入队失败不复位、TX排空、2 ms和毫秒回绕、关发射。可用既有`sensor_boot/tests/run_tests.ps1`重新构建该套应用入口测试。
- `& IR/tests/run_host_tests.ps1`通过：采样1190、协议/服务107、主控端口101次断言；VC98仍有既有整数窄化警告，目标ARMCC无警告。
- `& .venv/Scripts/python.exe -m unittest discover -s IR/tests -p test_uart_tool.py -v`通过10/10。默认串口检查工具不发RESET，真正串口本轮未打开。

入口原生测试模拟UART/GPIO/复位，不编译应用main或执行VTOR寄存器写；VTOR顺序本轮证据来自源码、AXF及向量/身份静态核对。构建、镜像和模拟通过不等于ARM实板跳转/中断、Flash擦写、光学效果或真实USB升级通过。共用引导/主控转发完整链路的既有软件证据见`sensor_boot/docs/verification_20261029.md`和`verification_20261029_usb_bridge.md`，本轮未重复全部链路测试。

后续只在确认实际目标/UID、供电、接线及要保留的主控功能分支后，另行安排工厂镜像安装/回读、默认安全电平、IR采样、真实USB升级和各阶段断电恢复。Python双路积木仍是后续独立工作。交付前也需整理现有未提交输入与固定SDK依赖；本轮未宣称源码已远端同步。
