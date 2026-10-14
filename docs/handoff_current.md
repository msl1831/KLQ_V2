# KLQ 主控当前交接（2026-10-14）

当前主控开发分支为 `codex/python-runtime`，工作树 `C:/Users/Administrator/.codex/worktrees/python-runtime/Codex`。生产固件源基线 `94435d1`，系统固件 0.8、板上引导 0.3；本轮测试工具和证据随当前分支同步。接力先检查 `git status --short --branch`、`git log -1`，以实际远端提交为准。

`F:/work/KLQ_V2/Codex` 仍为旧 `main`，有未提交的 UART4 语音、UL、电机和环境材料。本轮未修改这些材料、未合并分支。主控 Python 使用本分支，后续合流须保留旧 main 的本地成果；motor/voice 源码不在本分支，不能仅凭克隆本分支恢复整个产品。

## 当前结果

- 用户程序只在 16 KiB RAM，完成/停止/错误保留文本，新的 BEGIN 覆盖；复位/断电清空。无延时循环由协作执行保证可停止。
- 26 个非语音映射见 `docs/python_api_v0.1.md`，下载协议见 `docs/host_usb_protocol_v0.1.md`；内部调度/驱动不是上位机接口。
- 固件 0.8 修正 CS100A 有效样本状态 1 的距离等待；此前状态 0 的判断已复现并修正。
- 本轮真实 C 宿主模拟 22 组、164175 断言通过；实板 41 项功能断言通过。逐项范围和局限见 `docs/verification_20261014_blocks.md`，JSON 证据已入库。
- 主控 USB COM123、P2 CS100A 独立供电、SC7 在线。本轮结束状态 `USER_STATE=0, USER_VALID=0, UI=1, PY_ERROR=0`，测试程序已清 RAM。
- 监测服务监听 `127.0.0.1:8765`，本轮结束时已运行；COM 与进程 PID 接力时重新查。监测页自动重连、USB 断开会清旧数据显示断线原因。
- 未改生产固件，无新烧录；现有 BIN 20412 B、RO 20128 B、RW 26448 B，SHA-256 `1BE677DB4FD6854F60B6B8BC90BF1A46C4EFBB3282BF50ED453A47025F358ECF`。

## 复查命令

```powershell
python tools/test_blocks_sim.py --report build/blocks_sim.json
python tools/test_blocks_live.py --port <机器人CDC端口> --report build/blocks_live.json
python tools/status_dashboard.py
```

本机模拟采用已安装 VC98；GCC/Clang 需含汇编器与链接器，缺失时可用 `--cc` 显式指定完整工具链。模拟无串口操作；实板测试覆盖当前 RAM 程序，结束时软件复位清空 RAM。先停止监测服务再运行串口测试；自动测试期间不移动板子、不操作按键。

退出流程曾在 Windows CDC 重连时卡住。现已先保存功能报告，再用限时子进程发送 RESET/RUN；最终退出函数另行验证通过，主机和页面已恢复。保留功能测试与退出验证的证据边界，勿将电脑工具等待误称为固件用户循环卡死。

## 后续工作

1. 实物确认 PC3 按下/释放极性、SC7 安装前后左右、10 种表情与数字图案；本轮模拟和当前输入检查不替代这些验收。
2. 接入外接电机真实协议后，落实周期控制/保活，复测 250 ms 子板失联停机，验证 `motor_for` 与持续动作、双端口失败回滚及实际停车。当前模拟通过，实板启动仍报告错误 6。
3. 红外设备协议明确后实现条件判断；当前仅支持参数解析和“不支持”错误退出。语音暂不加入 26 项接口。
4. 继续保存 main 中语音、UL、电机与固定依赖，再经过构建/板测逐项合流；当前不要在脏 main 上直接切换或覆盖新固件文件。
