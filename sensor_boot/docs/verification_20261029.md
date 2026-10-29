# 共用引导软件验证 2026-10-29

工作目录 `F:/work/KLQ_V2/Codex`，分支 `main`，HEAD `63d1ea38f69fbd478f92751855f238340b6fa853`。本轮改动均未提交、未推送；原有主控/语音/UL/IR/MOTOR未提交内容保留。未访问探针、未烧录任何板卡。

编译器实查为 ARM Compiler 5.06 update 6 build 750 / MDK Plus 5.38，Python 3.12.14；Puya SDK 固定提交 `394627d99085c5eae72142503b0405b84428bb20`，本轮未修改 SDK。Flash/SRAM链接上限分别为引导6144 B、应用18304 B、SRAM3072 B；每镜像固定栈1024 B、堆0。

## 最终资源

| 板型 | 镜像 | BIN/ROM B | RO B | RAM B |
| --- | --- | ---: | ---: | ---: |
| ul | boot | 4464 | 4444 | 1600 |
| ul | app | 5776 | 5716 | 2016 |
| ir | boot | 4468 | 4448 | 1600 |
| ir | app | 6052 | 5980 | 2040 |
| motor | boot | 4456 | 4436 | 1600 |
| motor | app | 2856 | 2836 | 1448 |

引导增加的应用内部身份检查约104 B ROM，RAM无增量；应用的KID1固定身份占16 B RO。采用编译期板型、安全GPIO直接寄存器设置、148 B固定帧缓冲、128 B页缓存，无堆和运行时板型分派。链接区限制与BIN长度均通过。厂商SystemInit重设VTOR后，UL/IR在应用main中重定位到0x08001800再开中断，电机维持已有重定位流程。

## 产物与哈希

完整地址、长度、CRC、SHA-256及map读数固化在 [build_evidence_20261029.json](build_evidence_20261029.json)。每种release目录另有manifest。连续工厂BIN均24576 B，稀疏HEX只含引导、应用、16 B有效记录；身份区属于应用CRC覆盖范围。

| 板型 | 工厂HEX有效字节 | 应用CRC32 | 工厂HEX SHA-256 |
| --- | ---: | --- | --- |
| ul | 10256 | `0x82F15F6F` | `a6652f819315e0cea600b7abb55d035f5a66165605eba921a05386243b0d18b1` |
| ir | 10536 | `0x3CB2BDEC` | `62c9700f3c6e6c90c8efd973a3900798ed6559442a3693849e4ecd378c81744e` |
| motor | 7328 | `0xDFD41AD6` | `5563c7972b40728a1e70adc235e57fc50e95749a02c60dc7341454eb31d8904f` |

## 已执行的软件验证

```powershell
& sensor_boot/tools/build.ps1
& sensor_boot/tests/run_tests.ps1
& .venv/Scripts/python.exe -m unittest discover -s sensor_boot/tests -p test_tools.py -v
& IR/tests/run_host_tests.ps1
& MOTOR/tools/test_motor.ps1
& .venv/Scripts/python.exe -m unittest discover -s IR/tests -p test_uart_tool.py -v
```

- 三种引导和配套应用全部编译、链接并打包通过。应用嵌入身份放在0x080018C0；三种目标的向量表在map中均为192 B。
- C内核测试在UL/IR/MOTOR三种身份下通过：错板/兼容号/超长/奇数长度、乱序、页约束、重传、重复BEGIN/END、写入/记录提交失败、整包CRC、坏栈、应用内身份不符以及重启后的无有效事务。
- 编译实际引导main.c进行三种帧/命令测试通过：INFO/GET_LAYOUT、HOLD、BEGIN/DATA/END/RUN、未知命令/坏长度、CRC坏帧与超长头拒绝；不执行实际硬件跳转。
- 编译实际UL/IR device_service.c测试通过：进入引导、TX队列拒绝时不复位、发送完成后再复位、2 ms边界及毫秒回绕；IR确认关发射。
- Python新测试13/13通过：容器截断/CRC/地址/栈/内部身份、错误重新标记板型拒绝、全部工厂HEX记录校验和/地址/BIN一致性/元数据/ZIP/SHA-256；通过模拟UART向三个实际C引导实例传完整应用，覆盖分片读取、丢DATA应答重发、同上电续传、错板、UID/分区/offset异常、ENTER/RUN应答丢失、噪声/CRC/旧序号响应。
- 既有IR回归通过：采样1190、帧/服务107、主控端口101次断言；UART工具10/10通过。VC98对既有采样/主控端口代码仍有转换警告，本轮共用引导C测试无新增编译警告。
- 既有电机安全与独立升级内核测试通过。独立应用构建回归输出到build/sensor_boot_regression：UL ROM5756/RAM2016 B，IR ROM6024/RAM2040 B，电机ROM2840/RAM1448 B。UL较旧5744 B增加12 B，用于应答确实入队后才进入reset_pending及复位期间抑制普通命令。
- update.py --help通过；未打开真实COM口或运行实际更新。

## 尚未验证与接续工作

软件模拟仅验证C协议/更新状态机与工具交互；模拟Flash、GPIO、复位和跳转不构成芯片证据。待分别核实实际探针/目标/UID和供电后，安装对应工厂HEX并回读；测试冷启动默认电平、正常业务、UART升级、最大镜像、真实页擦写/读回、各阶段断电、复位/看门狗边界和重新下载恢复。电机测试须验证输出及机械行为；IR测试须确认发射极性和光学采样；UL测试须回归测距与TRIG。

本次未实现APM32主控USB下载入口、端口排他升级状态机或普通轮询暂停/恢复；后续按protocol_v1.md接入。没有RAM引导令牌、独立看门狗救援、旧镜像回退或签名认证。500 ms握手窗口需要主机及时HOLD，应用串口失控时仍需人工重新上电握手或SWD恢复。历史UL的末尾Load Flash规划被本目录低地址6 KiB方案替代，选项字节未改。
