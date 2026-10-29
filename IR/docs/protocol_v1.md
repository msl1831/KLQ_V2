# IR_V1 双路红外协议 1.0.0

基于 `UL/docs/klq_sensor_protocol.md` 的 KLQ1 公共帧：115200 8N1、3.3 V TTL，主机请求/设备应答，小端，16 B 头，payload 最多 132 B。CRC32/ISO-HDLC 覆盖头的前 12 B 与 payload；响应 payload 先放 `status:u32`，下文“数据”均不含这 4 B。坏 CRC/超长帧丢弃，半帧 20 ms 超时后清理。

## 设备身份与命令

INFO `0x0001`：请求空，数据 36 B，与 UL 公共 INFO 布局一致：

```text
protocol_version:u16, mode:u8, device_class:u8,
board_id:u16, hw_compat:u16, sensor_type:u16, reserved:u16,
fw_version:u32, capabilities:u32,
max_payload:u16, max_image_size:u16,
uid0:u32, uid1:u32, uid2:u32
```

IR 的值：protocol=1、mode=1、class=1、board=3、hw=1、type=2、reserved=0、fw=`0x00010000`、capabilities=7、max_payload=132、max_image_size=0。能力仅为 SAMPLE/STATUS/SELF_TEST；没有串口升级器或引导区。UL type=1，电机 class=2/board=2。主机先按类别、再按类型解码，不能凭插入的端口或相同命令号猜样本布局。

ECHO `0x0006`：请求 0～64 B，原样回送。RESET `0x0007`：请求空，只有 OK 响应成功进入 TX 队列后才关灯并停止采样/接收，等待发送完成且至少 2 ms 后复位。

GET_STATUS `0x0103`：请求空，数据 16 B：

```text
uptime_ms:u32, uart_error_count:u16, frame_error_count:u16,
measurement_count:u32, adc_fault_count:u16, driver_state:u8, reset_flags:u8
```

`measurement_count` 只统计完整采集对，`adc_fault_count` 统计初始化/转换故障（包括超时）；16 位计数自然回绕。reset_flags bit0～3 为看门狗/软件/NRST/上电复位，与 UL 一致。

SELF_TEST `0x0104`：请求空，数据 4 B：`uart_init_ok, adc_init_ok, measurement_seen, reserved`。`measurement_seen` 只说明至少完成过一对采样；不表示光学有效或已标定。SET_CONFIG `0x0102`、BOOT_HOLD `0x0009`、ENTER_BOOTLOADER `0x01F0` 返回 NOT_SUPPORTED；未知命令 BAD_COMMAND，已实现的空请求命令携带非空 payload 返回 BAD_LENGTH。

## GET_SAMPLE `0x0101`

请求空；只复制最新完整快照，不临时触发测量。成功响应数据固定 28 B（加状态后的 payload=32 B，整帧=48 B）：

| 数据偏移 | 字段 | 类型/单位 |
| ---: | --- | --- |
| 0 | sequence | u32，完整对或故障快照发布时增加 |
| 4 | capture_time_ms | u32，设备毫秒计数，支持回绕 |
| 8 | left_delta | i16，左关灯 ADC - 左开灯 ADC |
| 10 | right_delta | i16，右关灯 ADC - 右开灯 ADC |
| 12 | left_value | u16，0～1000 |
| 14 | right_value | u16，0～1000 |
| 16 | left_ambient | u16，左关灯 12 位 ADC |
| 18 | left_active | u16，左开灯 12 位 ADC |
| 20 | right_ambient | u16，右关灯 12 位 ADC |
| 22 | right_active | u16，右开灯 12 位 ADC |
| 24 | sample_status | u8，见下文 |
| 25 | quality_flags | u8，按位组合 |
| 26 | driver_state | u8，0 idle / 1 settle / 2 convert / 3 fault |
| 27 | reserved | u8，0 |

`sample_status`：0 NONE（没有完整样本）、1 VALID、2 DEGRADED（存在质量标志）、3 HW_FAULT。NONE 初始为全零；HW_FAULT 清零数值并更新时间/序号，故障数值不可当作“没有障碍/黑线”。故障期间通信仍可回应。

质量标志 bit0 左背景≤8、bit1 右背景≤8、bit2 左开灯≤8、bit3 右开灯≤8、bit4 左差分<0、bit5 右差分<0；bit6/7 保留为 0。背景过低表示环境光已接近接收电路下限，开灯过低表示反射可能削顶；负差分可能来自噪声或背景变化。DEGRADED 原始数据保留，主机应显式展示/处理标志。

归一化公式：`(max(delta,0)*1000+2047)/4095`，整数除法。0～1000 是 ADC 满量程比例，无黑白自动标定、无距离换算。反射越强通常值越大；饱和或快速变化的背景不能靠该差分完全恢复。

名义采样对周期 10 ms，每阶段 1 ms 稳定等待、丢 1 次再平均 4 次；左右交错，原子发布完整对。当前 main 主控每 20 ms 请求一次 IR 样本，UL 仍为 100 ms。主控、串口工具或以后 Python API 需按各自时钟观察序号是否推进，不能直接把设备 capture_time_ms 当作主控绝对时间。
