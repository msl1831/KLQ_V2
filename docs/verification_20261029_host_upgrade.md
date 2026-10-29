# 主控0.9：Python业务与外接固件升级合流（2026-10-29）

本轮授权为代码集成、构建、模拟验证和交付整理。**没有访问真实COM/USB/探针，没有烧录。** 0.9业务固件已完成软件合流；不能把以下结果当作实板验收。详细机器证据见 [构建/哈希/测试JSON](host_upgrade_build_evidence_20261029.json)。

## 基线与来源保留

开始时主控工作树为 `codex/python-runtime@ff33f97438f782a5844bf3ce6e2cd9ed120a402c` 且干净；包含固件0.8、16 KiB RAM解释器、26积木、PC4及显示/采样。来源 `F:/work/KLQ_V2/Codex` 为旧main@63d1ea3加未提交桥接/IR/板端/语音等工作。按三方差异合流，没有用main旧应用整文件覆盖最新业务，也未改写共享main的源码。

两侧文件、base、合并预览及哈希先保存在共享tmp/host_bridge_integration_20261029；另以git archive导出ff33f97独立重建。为使升级软件可交接，将sensor_boot和UL/IR/MOTOR第一方源码、构建脚本、原IO/文档纳入本分支，未导入嵌套.git、虚拟环境或旧build。三类固件44个生产C/头/启动/链接文件与当前共享来源逐字节一致；没有重写三类固件。

Puya SDK没有导入为裸gitlink，来源与固定提交由sensor_boot/dependencies.json声明，恢复方法见development_setup.md。本轮只读使用共享目录中干净的 `394627d99085c5eae72142503b0405b84428bb20`。旧main的语音试验及其它未提交材料保留原处，不在本次业务合流范围。

## 主控实现

- 复用单端口非阻塞桥接、token、UID/板型/布局门禁、结果保留与重传，支持UL class1/board1/hw1、IR class1/board3/hw1及PY32电机class2/board2/hw2。其它端口可继续普通发现/采样。
- 电机ENTER前先STOP确认；ENTER丢ACK或首次发现引导时自动HOLD，不等待电脑承担500 ms握手。BEGIN预算为10秒×3，电脑32秒结果期限；固定缓冲只留一个分包与响应。
- USB新增0x40～0x43，保留原Python协议和PY_ERROR/PY_LINE/PC4字段。桥接请求不复用旧USB瞬态缓存；每轮读256 B，复位/配置/溢出epoch取消旧会话，发送等待推进UART及PC4。
- 保留50 Hz SC7、运行25 Hz/待机10 Hz CS100A、80 ms样本新鲜度及3 ms日志发送。合入IR普通端口20 ms采样解析和观测格式。升级时暂停非请求USB日志，避免挤占升级握手。
- 修正合流独占边界：`user_program_start()`集中拒绝升级期间启动，实体PC4也不能绕过USB门禁；长按1.5秒先取消会话再关机。OPEN停止旧程序，关闭/取消/租约到期不自动恢复动作。USB reset只取消半包RAM下载，完整RAM程序仍保留。
- 日志最坏长度按字段上限计算：CS100A/未知设备138 B、IR127 B，均连同NUL放入原160 B缓冲；没有为日志增加栈缓冲。新增桥接无堆分配，原2048 B栈/512 B堆预留保持。

## 构建、资源与镜像

实际工具为ARMCC5.06 update6 build750、Python3.12.3、pyserial3.5；native为VC98 12.00.8168。ARMCC构建无错误，native保留既有C4244/C4761窄化/ABI转换警告。业务共存测试以C模式编译真实7个业务/协议模块；26积木宿主工具仍沿用既有VC C++模式，二者均不替代ARM目标构建。

| 主控镜像 | BIN/ROM B | RO B | RAM B | 相比ff33f97 BIN/RAM增量 |
|---|---:|---:|---:|---|
| 原0.8业务独立重建 | 20412 | 20128 | 26448 | 基线 |
| 合流0.9业务 | 24176 | 23884 | 26784 | +3764 / +336 |
| 原0.3引导独立重建 | 9844 | 9644 | 8736 | 基线 |
| 0.4引导回归构建 | 9944 | 9740 | 8744 | +100 / +8 |

应用范围 `0x08008000..0x0800DE6F`，SP `0x200068A0`、Reset `0x08008169`；引导 `0x08000000..0x080026D7`，SP `0x20002228`、Reset `0x08000169`。向量对齐、Thumb范围、主控32 KiB/489472 B分区与RAM上限均通过。HEX每条校验和、地址不重叠、完整覆盖BIN并逐字节相等；未进入0x0807F800元数据区域。主控引导没有链接解释器/外接升级应用模块。

应用SHA-256：`d255a4135526c10e24b18b01dd2dca1cbba46a5168b601bc348944355085a4b8`。引导回归SHA-256：`008fc6da22e3e869b7129ae2eab925b44708ea036a3c21520e6b3a9a7fefc4a6`。本功能只需安装0.9系统应用，已有主控引导0.3兼容，无需为此重刷引导。

三类镜像从导入的当前生产源码重新构建；当前manifest与共享来源最新manifest完全一致，未使用旧0.3主控ZIP：

| 板型 | 引导/应用 B | 应用CRC32 | 升级容器SHA-256 |
|---|---|---|---|
| UL | 4464 / 5776 | CF112888 | `0cf7cedb4d8dc572c2b81f51f7ba64469b8d6223f9ec93e1172a85e48e8fc1d3` |
| IR | 4468 / 6052 | 3CB2BDEC | `f1d90156e14801f8336de4ae5c7bcd42a042446c8bb3e3e5acbba75f70dcfafb` |
| Motor | 4456 / 2880 | 0897C604 | `070f16a977d9ca2fbdbad346c9d259f4bda3d94f82e97c22b30a2ae06df22fbf` |

已检查KID1身份、应用0x08001800向量/容量、末页有效记录、工厂HEX/BIN、容器CRC与manifest全部哈希。UL包含pending reset检查修复，Motor采用2880 B修复后应用，未被旧2856 B副本覆盖。

## 软件验证

- 真实端口桥接C：161断言；真实USB协议C：300断言。
- 真实Python解释器、RAM程序、UI、电源键、USB协议、端口桥接共同编译：176断言。验证无延时程序被OPEN停止、完整RAM保留、半包取消、PC4短按门禁、USB STOP、1500 ms关机前取消、错误再运行及30秒租约后不恢复动作。
- Python共30/30，无跳过，含4项完整链路测试。完整镜像现在覆盖三类设备×三个端口共9种组合：真实Python工具→真实主控C USB/端口→真实板端C引导，直到END/CRC/有效记录提交。另测错板拒绝、首个页后USB复位续传、DATA丢ACK耗尽后的重连与续传。
- 原26积木真实C模拟22组、164175断言通过；测试工具异常清理7项、USB格式2项通过。
- 三类共用引导更新/协议/模拟器及UL/IR升级入口通过；UL新增pending畸形请求回归通过；Motor安全/独立升级内核/应用进入引导通过。
- IR采样1190、服务协议107、主控端口104断言通过；104包含新补的CS100A主机时间戳及待机100/运行40 ms周期保持。

完整模拟保持在板端引导，不执行ARM应用跳转；USB硬件、Flash寄存器、GPIO/TC及主控发送等待的真实中断时序没有在native中执行。源文件哈希、资源和测试摘要已固化在JSON。重跑命令见development_setup.md。

## 交付与使用

新包：`build/host_bridge_0_9/release/klq_host_0.9_peripheral_updates.zip`，129839 B、23项，SHA-256 `a3a6e9d7a9fb4bb6d9334b050da3c3a221b2ab2949081be6b0190dafa5422237`。含0.9主控BIN/HEX、四个电脑工具、pyserial依赖、三类最新工厂/升级文件和manifest、中文说明与协议。重复打包得到同一哈希，ZIP CRC/所有嵌入文件哈希通过，解包后的update和klq_usb帮助入口可独立运行。

首次给外接板安装对应factory.hex后，示例命令（COM10及端口1均需按实际接线改）：

```powershell
python sensor_boot/tools/update.py --via-host --port COM10 --sensor-port 1 --image sensor_boot/build/ul/release/ul_update.klq
```

IR/Motor只替换image路径；三类设备均可接任意1～3口。电脑已有直连UART用法继续保留。升级包不会新增Scratch积木函数，上位机Python生成规则保持原接口。

## 明确未验收项

未做本轮真实USB枚举/UART/Flash/SWD、HOLD最坏握手、长BEGIN、拔插/电源中断恢复、实际运动/停车或ARM APP跳转。最新板上版本和供电须在下一次实测重新核对，不能把10月14日COM123/0.8状态当成今天实查。

共用板端引导RUN的发送/TC等待为void有界返回，超时后仍可能跳转；严格“应答完整发送后才跳转”尚不成立，本轮按分工保留并在协议中标注。电脑容忍RUN丢ACK不能替代这一底层保证。Python电机控制与IR条件驱动也仍是后续独立接入；本次升级功能不把模拟动作视作机械通过。
