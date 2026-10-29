# IR经主控USB升级实测交接（2026-10-29）

**最新结论：已完成。** 用户授权仅SWD更新引导后，采用128 B页RAM写入器安装1.0.2，APP/metadata/选项及新boot payload外字节完全未改；随后APP只经主机USB完整写入两次，各6052 B/48DATA、END/CRC/RUN及同UID应用确认通过，正常APP入口升级耗时3.468秒。最后30秒P1/P2各60条日志均有效且错误增量0，主控仍0.9、UI1/PY_ERROR0，观测PID28828（接力重查）。最终记录见verification_20261029_ir_via_host.md和ir_via_host_20261029.json。下列按过程保留的“失败/待授权/未写入”均是当时状态，不能覆盖本段最新结果。

补充：实际使用568 B RAM helper已留制品/源快照，仓库版本后续新增IPSR和MER/SER拒绝，为600 B，未在本轮硬件执行。最终PC工具要求引导至少1.0.2，未知/旧版本在BEGIN前拒绝。全Python41/41无跳过，Flash模型新400/旧140、三板native和目标构建通过。原1.0.0/1.0.1工厂镜像不再用于新升级；当前IR1.0.2，其它板未因此自动更新。USB后额外SWD全片读回遇probe already open而未完成，未强抢探针；不要把该可选检查记为通过。

用户明确要求通过主机给IR升级并验证此方式。当前主控权威工作树C:/Users/Administrator/.codex/worktrees/python-runtime/Codex，起始codex/python-runtime@8db761abff784cc3dd023a378b03def718a138c6且干净。共享F:/work/KLQ_V2/Codex现为main@72e2ac4，UL另有未提交开发；本轮不改共享文件或操作UL/电机。

## 当前硬件状态，勿误判恢复

- 主控COM123，KLQ ROBOT FW0.9，EXT_UPGRADE=1/EXT_PORTS=3，RAM用户程序空。目标IR是P2，UID4150354C/38323609/00DB8962，board3/hw1/class1/type2。
- 现场IR应用INFO为1.0.0/caps15，进入引导后现场INFO为1.0.1/caps8，APP0x08001800、容量18304、页128、metadata0x08005F80。其引导1.0.1已有共享IR/docs/reflash_boot101_20261029.md的15:15 SWD安装/回读记录；本轮没有访问DAPLink。
- 本轮两次升级均未发送任何DATA：第一次ENTER阶段主控报告UART reason2；第二次ENTER/HOLD/INFO/布局成功，但BEGIN返回设备status6。
- 随后只通过USB请求RUN恢复旧应用，设备仍返回status6、INFO保持mode0。**IR当前停留引导，旧应用未恢复，页面ONLINE只代表引导可响应INFO，ST=0/SEQ=0不是有效采样。** 可能已擦除有效记录，未直接读回Flash，不能声称APP正文和metadata完整。
- 观测服务在每次暂停串口后均恢复；最新PID31036，URL http://127.0.0.1:8765/。P1超声仍在线；主控未重新烧录。PID/端口接力前重查。

## 包与证据

IR升级容器在当前工作树与共享main逐字节相同：6072 B，应用6052 B、CRC3CB2BDEC，容器SHA f1d90156e14801f8336de4ae5c7bcd42a042446c8bb3e3e5acbba75f70dcfafb，应用SHA 19b7df54061df7583753b662352a77c93dd4d7bc6c8f0bf53ea2d22a6a653c54。目标是完整重写同一应用验证链路，不能靠版本不变/改变判断写入成功。

本机build/ir_via_host_20261029含：run_upgrade.py（调用真实update.upgrade/HostLink的限时记录器），attempt_01/与attempt_02/的完整应答/错误/工具哈希，前后网页快照，recovery_run.json及各次监控日志。首个失败先复制再重试，未覆盖原始证据。第二次BEGIN payload为03000100a417ecbdb23c；RUN恢复失败有单独记录。

## 已完成的软件修正

PC的host_link.py新增BridgeError(ValueError)，保留.reason；update.py只在ENTER收到typed reason2时立即重新INFO，并验证同UID、同board/hw、mode0，随后仍执行HOLD/INFO/GET_LAYOUT才允许BEGIN。其它错误、DATA期UART错误继续失败，不全局吞异常。未修改主控固件，目标Flash/RAM成本为0。

原工具13、USB工具13、实际C链路4（含三类×三口9次整包）全部通过；新增test_transition_recovery.py六项通过，含多种错误模式/UID/板型/布局/再次UART错误时零BEGIN、DATA错误不重启写入。第二次ENTER正常成功，未触发新恢复分支，不能声称该分支已由实板复现验证。

## 新发现的板端缺陷与下一步

共用boot_hw.c擦/写要求FLASH_SR.EOP=1，却没有启用FLASH_CR.EOPIE。普冉PY32F002B参考手册4.8.4明确EOP仅在EOPIE使能时置位；官方LL FLASH_PageEraseAndWrite例程先使能EOPIE，结束清EOP并关闭EOPIE。BEGIN先invalidate(metadata)再erase_app；metadata可能实际擦除却因EOP未置而返回6，导致原镜像失效。不能因未发送DATA就假定Flash未改变。

正在当前工作树合入共享main已安装1.0.1的RUN发送/TC保护，并准备修正EOP契约的引导1.0.2，独立输出build/ir_boot102_20261029，不能覆盖旧工厂交付或改动其它板固件。保留忙/错误/整页读回、地址限制及固定内存；需核对ARMCC map、manifest/HEX/CRC并做相关native回归。

此缺陷在常驻引导；应用.klq不能改引导，当前USB擦写又失败，不能用现有USB应用升级修复引导本身。**尚未授权本轮DAPLink/SWD重装，也未确认探针当前连哪块板。** 修正版具体镜像/哈希、构建及回归完成后，再请用户将DAPLink接到IR并明确同意安装修正版引导，然后回到主机USB重试完整48页/END/RUN和采样恢复。不得用SWD安装应用后冒称USB升级成功，不得操作当前其它对话使用的UL探针。

## 同轮后续授权和安装进度（以本节为最新状态）

用户已明确回复“已接到红外，同意仅更新引导”，授权先核对UID/备份，只装引导1.0.2，再经主机USB下载APP。保持主机USB与红外端口供电，禁止SWD写APP或metadata。

1.0.2已完成构建、范围及native验证，独立输出build/ir_boot102_20261029；boot/sensor_boot.bin为4516 B，SHA3bf5aef2f8191a7fb35d1d04f887e91f7bcf0e98920326abef1634af69f402f9；HEX SHAe37a4f29cc53a990d207553fc2a11e8ae42bcc26e58bc0700ba19ec346d123cb，实际范围08000000..080011A3。ROM4516/RO4496/RAM1600 B，相比1.0.1仅增加20 B ROM，保留RUN发送/TC保护。完整构建证据build_evidence.json；真实驱动寄存器模型新400/旧140 checks，旧驱动明确复现metadata已擦却返回失败。三类更新/解析、各22个RUN边界/2个默认启动、UL/IR入口回归通过。交付镜像已复制F:/work/KLQ_V2/Codex/outputs/ir_boot102_20261029。

D:\Python\Python312没有pyOCD；可用F:/work/KLQ_V2/Codex/UL/.venv/Scripts/python.exe，pyOCD0.45.1，明确选择本机SDK内Puya.PY32F0xx_DFP.1.2.6.pack。探针LU_2022_8888已连接，实读CPUID410CC601和UID4150354C/38323609/00DB8962匹配。

首次仅boot安装脚本build/ir_via_host_20261029/install_boot_only.py在任何擦写前被assert(region.blocksize==128)阻断，未写Flash。之后只读核实：DFP默认blocksize/get_sector_info=0x1000，page_size=0x80；直接标准sector erase会跨到APP0x1800，不能用keep_unwritten“擦后补回”冒充没碰APP。该失败记录位于swd_boot102_20261029_162134/result.json，written=false。

正在准备CPU在SRAM执行、按硬件128 B页擦写的专用引导修复器，以严格满足仅boot授权。代码区0x20000000..<=0x20000700、控制块0x20000800、页数据0x20000820、SP0x20000BF0；地址必须128对齐且限定08000000..080017FF，单页有界操作、EOPIE/忙/错误/读回/LOCK均需保留，结束BKPT。主线程需先备份整24 KiB/选项16 B，再逐页36次写新boot，读回4516 B并比较应用和metadata及选项完全未改，最后复位。不能使用标准4 KiB扇区编程器触及APP。

当前监测服务最新PID26692（安装guard失败后恢复），URL仍8765；IR仍mode0无有效APP采样，不能当作恢复。尚未发生任何SWD Flash写入，下一步等RAM修复器构建/审查就绪再执行，安装后修改run_upgrade.py允许从已确认mode0启动完整APP下载。用户的仅引导授权持续有效，无需重复询问；若IR目标UID不同则必须停止。
