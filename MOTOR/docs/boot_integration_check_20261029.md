# Motor 统一引导接入核对（2026-10-29）

工作区 `F:/work/KLQ_V2/Codex`，分支 `main`，HEAD `63d1ea38f69fbd478f92751855f238340b6fa853`，保留已有未提交内容。已读公共 handoff 第14/15节、development_setup 第8/9节及统一 UART/USB 转发协议。此记录仅针对 PY32 无码盘电机板；没有打开真实 COM、访问探针或烧录。

## 核对结果与修正

| 项目 | 当前实现/证据 |
| --- | --- |
| MCU/串口/SWD | PY32F002BF15U6TR，24 KiB Flash/3 KiB SRAM；USART1 PA3/PA4、115200 8N1；PA2/PB6 未被 Motor 初始化改写 |
| 分区 | 共用引导0x08000000/6144 B；APP 0x08001800/18304 B；0x08005F80独占128 B有效记录页；与 Motor 常量一致 |
| 向量/身份 | 共用 scatter 的 ER_VECTORS=192 B；0x080018C0 的16 B为`KID1, board=2, hw=2, app_base=0x08001800, ABI=1`；代码起于0x080018D0 |
| 构建 | `sensor_boot/tools/build.ps1 -Device motor` 内部调用 Motor 的 `-Target app -Bootloader -OutputDir ...`，编译 `app_identity.c` 并链接共用 scatter；已正确，无需重写 |
| VTOR/中断 | Vendor SystemInit 后，Motor main先关中断、设VTOR=0x08001800、DSB/ISB，再初始化输出/时钟/SysTick，最后开中断；已正确 |
| INFO | mode=1、class=2、board=2、hw=2、revision=2、version=2.0.0；KLQ_BOOT_APP下capability=9、无encoder位，96位UID；实际应用C测试核对通过 |
| 电机安全 | PA0/PA1同一DRV8837；有符号PWM±1000、20 kHz；切换先双零；STOP/非法PWM/UART错误/250 ms失联均归零；安全内核未修改，无码盘回调 |
| ENTER_BOOTLOADER | 先清请求PWM并停机，再发ACK；此次修正为ACK完整送入UART且TC确认发送结束时才软件复位，失败保持停机并留在应用 |
| GPIO初始化 | 原Motor代码将PA0/PA1 PUPDR配置为01上拉，存在输出模式建立前短暂弱上拉；改为10下拉，与共用引导一致。BRR预清零、再设输出及PWM零比较值的顺序保留 |
| 引导接续 | 无RAM令牌，复位后500 ms窗口由主控自动HOLD；坏记录/CRC停留引导。共用END/启动检查KID1/CRC；本轮未修改相关共用源码 |

修改只涉及 `MOTOR/firmware/app/main.c`、`MOTOR/firmware/platform/motor_hw.c/.h`、Motor 测试及本文/README。`reply` 和 UART drain 现在返回成功状态；ENTER 仅在两项均成功时复位。普通响应命令布局未改。为VC98实际应用测试给响应cmd加显式16位转换，未改变协议。

## 软件验证

```powershell
& MOTOR/tools/test_motor.ps1
& sensor_boot/tools/build.ps1 -Device motor
& .venv/Scripts/python.exe -m unittest discover -s sensor_boot/tests -p test_tools.py -v
```

- safety/update/app 三个实际 C 测试全部通过，最终 Motor 原生构建无警告。安全内核覆盖PWM边界、换向、零输出、250 ms边界和计时回绕；历史独立升级内核回归保留。
- 新 `MOTOR/tests/test_app.c` 直接编译实际应用main，使用假的UART/复位接口。检查INFO完整身份/capability=9，ENTER先停机再发字节，ACK成功排空后才复位；对20 B ACK的每一个发送失败位置分别验证不复位，TC等待超时也不复位且保持停机；STOP回归通过。
- 统一工具13/13通过，无跳过。构建完成后另重跑工厂HEX/BIN/manifest/ZIP核验与实际C引导整包下载两项，均通过，确保覆盖最终Motor镜像。原生C链路验证到END/CRC提交，不执行ARM应用、真实USART或Flash驱动。
- 直接检查最终BIN offset0xC0的KID1五字段、SP=`0x200005A8`（8字节对齐）、Reset=`0x08001975`（代码区内Thumb入口）；升级容器内应用与BIN一致。map中向量/身份/代码/数据区域和边界全部匹配。
- 共享引导源码、构建器/打包器/scatter/身份源、主控external_ports/protocol及公共handoff在本轮前后SHA-256一致。没有改写公共handoff、共享引导或主控转发；没有切换分支、合并python-runtime、提交或推送。

共享平台函数返回类型调整后，另在 `build/motor_boot_integration_20261029/standalone_app` 和 `standalone_boot` 构建历史独立入口，均通过：应用ROM2864/RAM1448 B、独立引导ROM4268/RAM1600 B。其无KID1的应用和历史工厂镜像只保留作回归，不能用于统一升级交付。

## 上轮应用核对的资源与交付快照

后续共用引导已更新为1.0.1；本节应用BIN/升级容器哈希仍有效，工厂文件与ZIP为修复前快照。当前工厂产物及三个板型回归见 [共用RUN修复验证](../../sensor_boot/docs/verification_20261029_run_tx.md)。

ARMCC5.06 update6 build750；既有固定Puya SDK。统一镜像各含1024 B固定栈，堆0。

| 镜像 | ROM/BIN B | RO B | RAM B | 相比本轮核对前 |
| --- | ---: | ---: | ---: | --- |
| 共用Motor引导 | 4456 / 6144 | 4436 | 1600 / 3072 | 无变化 |
| 配套Motor应用 | 2880 / 18304 | 2860 | 1448 / 3072 | ROM +24 B，RAM无变化 |

当前权威输出为 `sensor_boot/build/motor/release/`：

| 文件/内容 | SHA-256 |
| --- | --- |
| 共用引导BIN | `182c5c0a752911d177345ef50ed817781e4f178f74e90e2687576c22c42cf058` |
| 配套应用BIN | `8272fc74e30b5e2bcbc28a904a5d29b0e43e02ed1663ab5f1e8a1556d4bd9b07` |
| `motor_factory.hex` | `ef131d1d59e79a4a476de943923df7c31527cebb805cf8e79c155a48c3282ed3` |
| `motor_factory.bin` | `31e18df42a3dd56b18ea0588fc3989ee5da02e91b50d8d470ad8c26387898c2a` |
| `motor_update.klq` | `070f16a977d9ca2fbdbad346c9d259f4bda3d94f82e97c22b30a2ae06df22fbf` |
| `motor_release.zip` | `9d1cc2403c507a1d739b64d6727cd698b2ea62be9fa19f2845c45f93dc531777` |

应用CRC32=`0x0897C604`，工厂BIN24576 B，HEX有效内容7352 B，升级容器2900 B。当前release的manifest/ZIP已由统一构建同步生成。本表替代此前Motor的2856 B统一应用快照；共享历史verification/build_evidence和 `build/usb_sensor_bridge/release/klq_usb_sensor_bridge_0.3.zip` 未改，该主控ZIP中的Motor升级包仍是本次修正前快照。使用本目录新生成的Motor升级包，不用旧ZIP中的副本覆盖它。

后续固定入口：

```powershell
& F:/work/KLQ_V2/Codex/sensor_boot/tools/build.ps1 -Device motor
```

首次SWD安装使用 `sensor_boot/build/motor/release/motor_factory.hex`，其包含共用引导、配套应用和有效记录。后续经主控升级使用同目录 `motor_update.klq`；电脑命令中的COM和端口号要按实际接线选择，例如：

```powershell
& .venv/Scripts/python.exe sensor_boot/tools/update.py --via-host --port COM10 --sensor-port 3 --image sensor_boot/build/motor/release/motor_update.klq
```

此为使用示例，本轮未执行。`MOTOR/tools/build_app.ps1` 默认独立构建及 `MOTOR/tools/pack_factory.py` 是历史入口，不能生成统一交付包；仅用 `build_motor.ps1 -Target app -Bootloader` 也只是生成配套APP，完整工厂和升级包应走上面的统一构建器。

## 尚待处理的共用项与实板边界

前轮发现的共用RUN发送/TC超时仍跳转问题，现已按用户后续授权修复：发送和TC状态逐层返回，RUN先锁定HOLD，只有完整发送成功才跳转，失败后不会走500 ms自动启动旁路。UL/IR/Motor的软件回归均已通过，详见 [共用RUN修复验证](../../sensor_boot/docs/verification_20261029_run_tx.md)。

新板仍未烧录或实测。GPIO/TC标志、VTOR中断、Flash擦写、冷启动/复位电平、低占空比正反转和250 ms失联停机，以及实际USB→UART的HOLD时序、升级断电恢复都须另行验收。无独立看门狗、无旧镜像回退和RAM令牌，不能把软件模拟当作芯片或机械行为验证。HK32历史材料保持原样。
