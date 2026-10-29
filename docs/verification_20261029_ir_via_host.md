# IR经主控USB升级及修复（2026-10-29）

用户要求“通过主机给红外升级固件，验证这种方式”。本轮最终完成两次实际整包升级：PC→主控COM123/FW0.9→外接P2 UART→IR，6052 B、48个DATA事务从offset0开始，END/CRC、RUN及同UID应用INFO均成功，随后30秒采样稳定。过程曾失败并修正，不能将最初版本描述为直接通过。机器证据见 [汇总JSON](ir_via_host_20261029.json)，详细过程见 [工作记录](ir_via_host_worklog_20261029.md)。

## 设备、包与授权

IR实际UID为4150354C/38323609/00DB8962，board3/hw1/class1/type2。安装前APP1.0.0/caps15、引导1.0.1/caps8，身份和布局均由现场INFO核对。共享main@72e2ac4有其先前SWD安装1.0.1记录，本轮不改共享main或其它任务的UL代码。

升级包与两边当前IR应用相同：容器6072 B，应用6052 B，CRC3CB2BDEC；容器SHA f1d90156e14801f8336de4ae5c7bcd42a042446c8bb3e3e5acbba75f70dcfafb；应用SHA 19b7df54061df7583753b662352a77c93dd4d7bc6c8f0bf53ea2d22a6a653c54。此次是同一APP1.0.0重新写入验证，不能只看版本号判断成功。

发现常驻引导缺陷后，用户另行明确回复“已接到红外，同意仅更新引导”。SWD仅用于UID核对、备份及更新IR引导，**APP和metadata没有通过SWD改写**，随后应用仍完整走主控USB链路。未烧录主控或UL/MOTOR。

## 发现和修复

第一次ENTER约31 ms返回主控桥接UART错误，尚未BEGIN。PC现以BridgeError保留reason，仅ENTER的reason2允许立即重新INFO并确认同UID/板型/兼容号/mode0，再继续HOLD/INFO/布局；其它错误及DATA错误不吞掉。主控0.9源码/镜像未修改。恢复分支有专用软件注入回归，后续正常实板尝试未触发该分支，不能冒称已实板故障注入通过。

第二次ENTER/HOLD/引导1.0.1 INFO/布局成功，BEGIN却返回设备status6，未发送DATA。原引导要求EOP但没有使能EOPIE；普冉参考手册4.8.4及官方LL例程规定EOP仅在EOPIE使能后置位。后续24 KiB备份实读确认metadata页128 B全FF、APP正文哈希仍为原值：metadata已擦除却被判失败。安全RUN随后也返回6，设备保持mode0，不能把页面“在线”当作恢复应用。

引导1.0.2在擦写前使能EOPIE，完成后清EOP、关闭EOPIE再锁Flash；保留忙/错误/地址/整页读回校验，并合入1.0.1的RUN完整应答及TC保护。PC工具在BEGIN前要求引导至少1.0.2，旧1.0.0/1.0.1/未知版本均拒绝，避免另一设备再次失去有效记录。

## 仅引导安装证明

boot1.0.2 BIN4516 B，范围08000000..080011A3，SHA3bf5aef2f8191a7fb35d1d04f887e91f7bcf0e98920326abef1634af69f402f9；HEX SHAe37a4f29cc53a990d207553fc2a11e8ae42bcc26e58bc0700ba19ec346d123cb。RO4496/RAM1600 B，比1.0.1只增20 B ROM。SP20000640、Reset080001A5。IR APP和容器未变。

pyOCD0.45.1、指定LU_2022_8888/DFP1.2.6、CPUID410CC601及MCU UID均核对。DFP默认擦除扇区4096 B，直接写4516 B会跨到APP0x1800；首个安装方案在擦写前被guard拒绝，没有使用“擦后补回APP”代替仅引导授权。

实际使用SRAM内568 B代码，入口20000001、控制20000800、128 B页数据20000820、MSP20000BF0。先备份整24 KiB和16 B选项，36页逐页擦/写/读回，均result0/stage9。最终整24 KiB比较：4516 B新boot与镜像完全一致，payload之外所有字节（包括APP、metadata）和选项完全未改。证据目录build/ir_via_host_20261029/swd_pages_20261029_163906。

当前仓库helper另加IPSR/MER/SER前置拒绝，600 B、RO600/RW0、栈8 B，尚未上硬件；保留本轮实际使用568 B的制品和源快照，不混淆两者。helper及其CPU/RAM写入不是USB应用升级实现，仅用于此次获批的引导修复。

## USB实际结果

| 尝试 | 起点及结果 |
|---|---|
| 1 | APP→ENTER UART错误；未BEGIN |
| 2 | APP→boot1.0.1→BEGIN status6；无DATA，不能启动旧有效镜像 |
| 3 | boot1.0.2→BEGIN offset0→48包/6052 B→END→RUN→同UID APP1.0.0 |
| 4 | APP1.0.0→ENTER/自动HOLD→boot1.0.2→布局→offset0完整48包→END→RUN→同UID APP，升级阶段3.468秒 |

DATA依次确认128、256……6016、6052，末包36 B；END由真实引导校验整镜像CRC并提交记录，RUN收到应答。CLOSE成功，主控ECHO正常，仍FW0.9/UI1/USER_STATE0/USER_VALID0/PY_ERROR0。最终观测服务已恢复，保持用户页面运行。

随后30秒P1/P2各60条不同端口日志全部在线、ST1/Q0；两口TO/FE/UART增量均0，IR序号36690→39636。序号为设备发布计数，不能据此宣布主控UART帧率。现有累计错误包括调试暂停和旧记录，使用增量判定本观察窗口。

曾尝试额外只读SWD核对USB之后的整片Flash，但探针返回already open，未强抢、未成功读回；因此本轮APP写入证据为实际DATA偏移、板端END/CRC、启动及连续采样，不声称USB之后又做了整片SWD读回。引导修复阶段的整片比较已成功，二者证据范围不同。

## 复现与边界

使用当前分支工具及板端引导至少1.0.2，端口号按实际改：

```powershell
python sensor_boot/tools/update.py --via-host --port COM123 --sensor-port 2 --image build/ir_boot102_20261029/release/ir_update.klq
```

默认输出也可重新生成，但不能复用旧0.9合集内的factory或旧PC工具。独立构建：`sensor_boot/tools/build.ps1 -Device ir -OutputDir build/ir_boot102_20261029`，SDK路径见development_setup.md。

全Python测试41/41、0跳过，包括5个版本门禁方法/36个场景及三类×三口真实C整包链路；真实Flash驱动模型新400/旧140检查通过，明确复现旧版metadata已擦却返错；三类native更新/解析、各22个RUN边界和2个默认启动通过。UL/MOTOR仅新增配置构建验证：boot4512/4508 B、RAM1600 B，没有升级这些实板。IR实板正常升级通过不等于断电/拔插/发送故障注入验收，也没有解决先前记录的非人为IR掉线根因。
