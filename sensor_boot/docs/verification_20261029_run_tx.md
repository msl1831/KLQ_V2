# 共用引导 RUN 发送超时修复与三板回归（2026-10-29）

用户授权修复共用引导RUN发送超时仍跳转，并回归UL、IR、Motor，明确暂不烧录。工作区为 `F:/work/KLQ_V2/Codex`，`main@63d1ea38f69fbd478f92751855f238340b6fa853` 加原有未提交改动。没有访问真实COM、USB设备、探针、GPIO或Flash，没有烧录、切换分支、提交或推送。

## 修复行为

`send_response`、`sensor_hw_uart_drain` 和 `boot_uart_drain` 改为返回成功状态。RUN先设置HOLD，再检查有效镜像；仅在镜像有效、20 B ACK完整送入UART且TC确认发送完成后跳转。字节发送失败或TC等待超时均留在引导，允许重发RUN。HOLD同时堵住原有500 ms自动启动逻辑，否则主循环可能在RUN失败返回后立刻自动跳走。

引导版本从1.0.0升为 **1.0.1**（INFO=`0x00010001`），帧格式、命令号、板型身份、KID1和Flash分区保持一致。普通复位后的有效应用仍在500 ms窗口结束后自动启动；无有效镜像仍停在引导。引导安全GPIO初始化未改，电机保持双低、IR关闭发射、UL触发和LED保持低。

修改范围：`sensor_boot/src/main.c`、`src/sensor_hw.c`、`src/boot_hw.c`、对应声明头文件与版本常量、实际协议测试和说明文档。三类应用源码、主控转发、打包器、SDK、选项字节和历史Motor独立引导未改。原有已暂存gitmodule等内容保持原状。

## 缺陷复现与回归

先添加实际主循环测试并接通drain状态接口，在尚未修正RUN逻辑时运行，UL在 `hold && image_ok && valid && ms>=500U` 断言失败，复现失败RUN未阻止启动的问题；随后修改RUN并对三个身份全部通过。

`test_protocol.c` 直接包含生产引导main，测试用串口/Flash/跳转替身运行实际循环，将时间推进至600 ms且不预先发送HOLD：

- 对20 B RUN ACK的每一个发送位置分别制造失败，验证不跳转、不进入TC等待、有效记录不受影响，后续重发RUN能成功。
- ACK完整写出但TC超时时，验证不跳转；继续执行多轮主循环，确认500 ms自动启动也被阻止；恢复TC后重试成功。
- 成功路径确认完整ACK和一次TC排空后才跳转；无RUN时正常启动、有坏镜像时保持引导两个默认路径也通过。
- 每板22个RUN边界场景和2个默认启动场景，共三板；既有INFO/GET_LAYOUT、HOLD、BEGIN/DATA/END/RUN、CRC/长度/身份/页限制和重传测试保留。INFO版本明确核对1.0.1。

执行命令：

```powershell
& sensor_boot/tests/run_tests.ps1
& sensor_boot/tools/build.ps1
& IR/tests/run_host_tests.ps1
& MOTOR/tools/test_motor.ps1
& sensor_boot/tests/run_host_bridge.ps1
& sensor_boot/tests/usb_host/run_tests.ps1
& sensor_boot/tests/run_pipeline_tests.ps1
& .venv/Scripts/python.exe -m unittest discover -s sensor_boot/tests -p 'test_*.py' -v
```

结果全部通过。UL额外的实际应用ENTER测试也通过，包括ACK、TX排空、计时回绕及复位待执行时抑制合法/畸形请求。原UL测试输出目录的既有OBJ无法覆盖，未删除或修改其权限；使用同一测试源/实际device_service、frame_codec及相同编译参数，输出到 `build/sensor_boot_run_regression_20261029/ul_entry` 后通过。

三板共用C内核/实际帧解析与UL/IR服务测试通过；Motor安全、独立更新和实际APP测试通过；IR采样1190、协议107、主控端口101次断言通过。主控桥接161、USB协议300次断言通过。Python **30/30通过、无跳过**，包括对三个最终应用通过实际C USB→UART→引导更新内核完成END/CRC提交、断连续传和错误清理。原生测试不执行ARM应用跳转或真实Flash/USART。VC98在既有IR/主控源上仍报告原有转换警告，新共用引导测试没有新增编译警告。

## 重建、资源及产物

ARMCC5.06 update6 build750、固定Puya SDK和原有Python。引导上限6144 B、应用18304 B、SRAM3072 B；固定栈1024 B、堆0。三板均重建引导和应用，并重新打包工厂HEX/BIN、manifest和ZIP。

| 板型 | 引导 ROM/BIN（增量） | 引导 RO | 引导 RAM | 应用 ROM / RAM |
| --- | ---: | ---: | ---: | ---: |
| UL | 4492 B（+28） | 4472 B | 1600 B | 5776 / 2016 B |
| IR | 4496 B（+28） | 4476 B | 1600 B | 6052 / 2040 B |
| Motor | 4488 B（+32） | 4468 B | 1600 B | 2880 / 1448 B |

所有map范围与上限通过。三份APP BIN和升级容器的SHA-256与本轮开始时完全相同；应用版本仍为UL1.0.1、IR1.0.0、Motor2.0.0。前192 B向量、0x080018C0的16 B KID1和ABI=1保持正确。各工厂HEX的记录校验和、地址、BIN内容、末页有效记录、manifest哈希、ZIP内容一致性均由工具回归验证通过。

| 最新工厂HEX | SHA-256 |
| --- | --- |
| `sensor_boot/build/ul/release/ul_factory.hex` | `70cbf0f9effdeeda2c0452122600dfdedfbcc5b85623b366cf62f60adbd1c397` |
| `sensor_boot/build/ir/release/ir_factory.hex` | `3e7534c7e4e4794d237140489b3455f7c650e5a6d6b485cc20aad648ef857c15` |
| `sensor_boot/build/motor/release/motor_factory.hex` | `d98ded1908a0971047dc4524c9e1a961d30acca0401a491d15795da55333d230` |

完整前后manifest、ROM/RAM、APP身份、ZIP和各镜像哈希保存在 [run_tx_build_evidence_20261029.json](run_tx_build_evidence_20261029.json)，本记录替代旧验证表中的引导资源/工厂文件快照。构建仍走 `& sensor_boot/tools/build.ps1`，或加 `-Device ul/ir/motor`。

本次修复位于引导区，后续安装须使用对应新版 `*_factory.hex`；`*_update.klq`只更新应用，不能把修复写进已有引导。各板型release目录已更新，旧主控合集ZIP/历史验证快照未重写，跨电脑交接时应重新收集当前产物。为避免共享交接文档冲突，本轮新增证据保存在本目录，公共handoff未修改。

## 实板边界

本轮未烧录。等待后续授权并核实探针/板型/UID后，再安排新版引导安装及回读、实际UART TXE/TC超时与正常RUN、上电/复位/HOLD时序和三类业务验证。软件模拟已验证异常分支和状态机，不等同于芯片发送时序、Flash写入或实际电机/光学/测距验证。
