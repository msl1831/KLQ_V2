# IR 首版软件验证（2026-10-26）

范围：`main@63d1ea38f69fbd478f92751855f238340b6fa853` 加当前未提交工作区。保留原主控语音、UL、电机等内容，没有切分支/合并/提交/推送，没有连接或烧录设备。本记录不证明 ADC 外设、真实光学效果、实物刷新率或 Python 积木已验收。

## 构建与资源

工具 ARMCC 5.06 update 6 build 750；PY32 SDK `394627d99085c5eae72142503b0405b84428bb20`，SDK 工作区干净，未修改。命令：

```powershell
& IR/tools/build_sensor.ps1 -Configuration Release
& .venv/Scripts/python.exe tools/build.py --demo --output-dir build/ir_host_after
& .venv/Scripts/python.exe tools/build.py --output-dir build/ir_boot_check
```

IR、主控应用及主控引导均通过，无 ARMCC 编译警告。主控旧构建目录中的目标文件不可覆盖，因此增加通用 `--output-dir` 参数，使用独立目录完成修改前后重建，未改变默认构建目录或镜像布局。

| 目标 | ROM/BIN | RO 执行区 | RAM（RW+ZI） |
| --- | ---: | ---: | ---: |
| IR 1.0.0 | 6024 B | 5952 B | 2040 B |
| 本轮前主控工作区 | 15988 B | 15764 B | 26384 B |
| IR 接入后主控工作区 | 16508 B | 16284 B | 26432 B |
| 主控增量 | +520 B | +520 B | +48 B |
| 主控引导回归重建 | 9272 B | 9084 B | 8728 B |

IR Flash 限制 20352 B、RAM 3072 B；RAM 包含固定 1024 B 栈、0 B 堆。主控应用限制 Flash `0x77800`、RAM `0x1FFF8`，均未越界。栈分配通过链接，尚无实板栈高水位证据。

IR 输出 `IR/firmware/app/build/ir_sensor_app.hex/.bin`：

- BIN SHA-256 `665cebf3349df19f468c1ae5fba14a7f42c81c05f31cfc8f64e5131d2b2fac16`。
- HEX SHA-256 `3bb079544c2f0ac5629b44f51077b44c04c6fda1d29a7e0dbfcaee904eab2b90`。
- 独立解析 Intel HEX 各记录校验、地址及 BIN 对比通过：有效地址 `0x08000000..0x08001787`，6024 B，未含选项字节/芯片保留尾区。
- 主控 BIN SHA-256 `9ac56dc284564440b4a6821c177b0d566e19e91d04def8fa410cad23de90d6e9`，输出 `build/ir_host_after/klq_demo_app.bin`；含本机原有未提交语音改动，不是仅凭当前 Git 提交可重建的发布版本。

## 软件测试

```powershell
& IR/tests/run_host_tests.ps1
& .venv/Scripts/python.exe -m unittest discover -s IR/tests -p test_uart_tool.py -v
```

Native 测试使用已安装 VC98 CL 12.00，把实际 C 源文件与模拟 GPIO/ADC/UART/时间绑定，不是复制算法的 Python 模型：

- 采样 1190 次断言：左右发射隔离、首次丢弃/四次平均、整对原子发布、正负差分、归一化上下界、质量标志、毫秒回绕、亮灯时 ADC 故障立即关灯/清零发布、100 ms 恢复重试及不追赶历史周期。
- 协议/服务 107 次断言：已知 CRC、最长 payload、65535 长度拒绝、独立身份、28 B 布局、错误长度/CRC/超长/半帧恢复、升级拒绝、ACK 入队失败不复位、发送完成后复位、复位期间停采关灯。
- 主控端口 101 次断言：实际 `external_ports.c` 类型与长度分派、UL 16 B/IR 28 B、负差值、0～1000/ADC 范围及驱动状态约束、未知类型/电机/引导不误采样、响应超时/发送 TXBE 超时解绑后清除旧身份/数据。发送失败保留旧在线数据的历史分支已修复；临时测试副本移除解绑修复时，回归测试准确失败，随后恢复副本。

这些断言包含循环观察点，不能等同同样数量的独立硬件场景。VC98 对窄整数参数有预期转换警告；目标 ARMCC 构建无警告。寄存器级 ADC 初始化/校准未在模拟测试中执行。

串口 CLI 的模拟 UART 单测 10/10 通过，检查默认不发 RESET、显式复位、错误传感器身份拒绝、ADC 自检失败/硬件故障退出、CRC及样本格式/序号推进校验。单次或短间隔为诊断模式；多次且间隔≥20 ms 时必须观察到至少两个有效/降级样本序号，避免通信成功却总是旧快照。没有打开真实串口。Dashboard 对 IR→UL→离线的解析检查通过，Python 语法检查通过；没有启动页面或连接主机。既有 `tools/test_protocol.py` 的两个格式/镜像边界单测通过，未运行其可选实板部分。

## 剩余实板验收

1. 确认 IR 目标/探针、供电、公共地和 UART，按授权另行烧录，先检查启动/SWD/两灯默认关闭。
2. 核对左右 PB1/CH0、PB0/CH7 与实际接线，测量灯开关稳定时间、原始 ADC、饱和标志和串扰。
3. 用黑白表面与障碍物，在不同环境光下记录差分及 0～1000 数值；按需要确定黑白标定/增益、刷新率与软件滤波。
4. 实测 INFO 类型区分、完整样本更新、主控拔插/失联/重连和复位 ACK；另在最新 Python 分支确定左右 API。

构建输出和测试生成物被忽略；源码、文档及 IO 工作簿均需后续明确提交才能迁移。SDK 仍为既有 UL 嵌套仓库，跨电脑依赖恢复遵循 `docs/development_setup.md`。
