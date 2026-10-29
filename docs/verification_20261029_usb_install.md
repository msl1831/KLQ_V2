# 主控0.9通过自身USB安装（2026-10-29）

这是同日软件合流之后、用户明确要求“通过主机USB升级主机固件”的实板安装记录。[前阶段合流记录](verification_20261029_host_upgrade.md)的无硬件边界保留；本记录只补充此次实际执行的范围。原始应答和逐步结果见 [安装JSON](usb_install_20261029.json)。

- 源码提交：codex/python-runtime@ce714f7f19a0e23156f4d92e5ea2c3a82c73ee4a。使用已构建、模拟验证并打包的应用，没有重新修改固件源码。
- 唯一KLQ USB设备为COM123，VID:PID=314B:0108，序号000000000000，位置1-4.2。另一COM92是DAPLink，本次未访问。
- 安装前INFO：KLQ ROBOT FW 0.8，VALID=1、USER_STATE=0、USER_VALID=0、UI=1、PY_ERROR=0，RAM没有用户程序。
- 镜像：24176 B，SHA-256 d255a4135526c10e24b18b01dd2dca1cbba46a5168b601bc348944355085a4b8；局部向量、链接区间及证据JSON一致。目标应用区0x08008000，实际镜像末字节0x0800DE6F；主控引导未重写。

## 实际过程与结果

1. USB RESET应答成功，重新查询得到KLQ USB BOOT 0.3。先核对APP=0x08008000、MAX=489472、PAGE=2048，再允许BEGIN擦写。
2. 使用tools/klq_usb.py flash分24包传入24176 B，所有提交偏移正确，END成功并返回整包CRC通过。后续引导INFO为VALID=1、RECEIVED=24176。
3. USB RUN成功。跳转后第一次打开COM123遇到Windows重新枚举的FileNotFoundError，重试一次恢复；未再次写入或修改电脑USB设置。
4. 系统INFO确认KLQ ROBOT FW 0.9、VALID=1、USER_STATE=0、USER_VALID=0、USER_LENGTH=0、UI=1、PY_ERROR=0，以及EXT_UPGRADE=1、EXT_PORTS=3。
5. ECHO发送/返回KLQ_FW_0.9_USB_OK完全一致，校验应用运行后的USB请求/应答正常。最终保留系统固件0.9运行待机，原引导0.3保留，未下载示例Python。

USB命令在独立限时子进程执行，复位/跳转后重新枚举端口，不把一次瞬态打开失败当作固件故障。Flash写入由引导END校验和有效记录确认；本次没有使用SWD逐字节读取Flash。

## 验证边界

本次证明主控自身USB升级、引导CRC提交、应用跳转、0.9状态和USB回显。未执行外接UL/IR/电机升级，也未重测实际电机停车、PC4/PC5、电池冷启动、SC7/外接采样或断电恢复；同日合流记录中的这些待验收项保持。未查询/操作DAPLink、外接Flash或外设固件。点阵实物观感未要求用户重新确认，UI=1为软件状态回读。
