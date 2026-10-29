# KLQ 主机—外接设备串口协议 V1

本文定义 KLQ 主机任意外接 UART 端口与传感器/电机设备之间的公共通信协议。当前首个实现是 `PY32F002BF15U6TR + CS100A` 超声波传感器应用固件；主机固件与传感器固件分别构建和下载。

## 1. 物理层和角色

- `115200 bit/s`、8 数据位、无校验、1 停止位、无硬件流控，3.3 V TTL。
- KLQ 主机始终发起请求，外接设备只响应请求；一个端口同一时刻只允许一个未完成请求。
- KLQ 串口 1～3 不固定设备类型。主机通过每个端口的 `INFO` 响应识别设备类别、具体类型和运行模式。
- 正式通信 UART 不发送裸文本日志。

## 2. 帧格式

所有多字节整数均为小端序。请求和响应共用 16 字节头：

| 偏移 | 长度 | 字段 | 说明 |
| ---: | ---: | --- | --- |
| 0 | 4 | magic | ASCII `KLQ1` |
| 4 | 4 | sequence | 主机请求序号，响应原样返回 |
| 8 | 2 | command | 请求命令；响应为请求命令 OR `0x8000` |
| 10 | 2 | payload_length | payload 字节数 |
| 12 | 4 | crc32 | CRC-32/ISO-HDLC |
| 16 | N | payload | 最大 132 字节 |

CRC 参数为多项式 `0x04C11DB7` 的反射形式、初值 `0xFFFFFFFF`、输入/输出反射、结果异或 `0xFFFFFFFF`，等同 Python `binascii.crc32`。CRC 覆盖头部偏移 0～11 和 payload，不包含 CRC 字段自身。

响应 payload 的前 4 字节固定为 `status:u32`，其后才是命令数据。因此普通响应数据最多 128 字节。主机超时后可用相同 sequence 和完全相同的请求重发。

## 3. 公共枚举

### 3.1 状态码

| 值 | 名称 | 含义 |
| ---: | --- | --- |
| 0 | OK | 成功 |
| 1 | BAD_COMMAND | 未实现命令 |
| 2 | BAD_LENGTH | payload 长度不符合命令 |
| 3 | BAD_CRC | CRC 错误；当前解析器丢弃坏帧而不响应 |
| 4 | BUSY | 设备忙 |
| 5 | NOT_SUPPORTED | 命令已定义但当前固件不支持 |
| 6 | INTERNAL_ERROR | 内部错误 |

### 3.2 设备身份

| 字段 | 当前值 | 含义 |
| --- | ---: | --- |
| mode | 1 | 应用模式；后续引导模式使用 0 |
| device_class | 1 | 传感器；电机等设备分配其他类别 |
| board_id | 1 | UL_V1 板 |
| hw_compat | 1 | UL_V1 硬件兼容号 |
| sensor_type | 1 | CS100A 超声波 |

主机先按 `device_class` 选择设备服务，再按 `sensor_type` 选择业务解码。引导程序也必须返回同样的板级身份，才能在写入前拒绝错误镜像；具体测量功能只在应用固件内实现。

## 4. 当前应用命令

### INFO `0x0001`

请求无 payload。成功响应数据为 36 字节：

```text
protocol_version:u16, mode:u8, device_class:u8,
board_id:u16, hw_compat:u16, sensor_type:u16, reserved:u16,
fw_version:u32, capabilities:u32,
max_payload:u16, max_image_size:u16,
uid_word0:u32, uid_word1:u32, uid_word2:u32
```

当前 `fw_version=0x00010001`（1.0.1，增加 PA6 工作指示灯），能力位 bit0/bit1/bit2 分别表示 SAMPLE、STATUS、SELF_TEST。

### ECHO `0x0006`

请求携带 0～64 字节，响应原样返回这些字节。用于端口发现后的链路诊断。

### RESET `0x0007`

请求无 payload。设备发完 OK 响应后软件复位。

### GET_SAMPLE `0x0101`

请求无 payload。成功响应数据为 16 字节：

```text
sample_sequence:u32, capture_time_ms:u32,
echo_ticks:u16, distance_mm:u16,
sample_status:u8, quality_flags:u8, driver_state:u8, reserved:u8
```

`echo_ticks` 每计数 2 us。样本状态：0 无样本、1 有效、2 小于 20 mm、3 超量程、4 未收到回波、5 硬件故障。当前 `quality_flags` 预留为 0。读取样本不会阻塞或额外触发，设备后台以“上次完成后间隔 1 ms”方式尽快连续测量。

### GET_STATUS `0x0103`

请求无 payload。成功响应数据为 16 字节：

```text
uptime_ms:u32, uart_error_count:u16, frame_error_count:u16,
measurement_count:u32, timeout_count:u16,
driver_state:u8, reset_flags:u8
```

`reset_flags`：bit0 看门狗复位、bit1 软件复位、bit2 NRST 复位、bit3 上电复位。

### SELF_TEST `0x0104`

请求无 payload。成功响应数据为 4 字节：`uart_init_ok`、`timer_init_ok`、`measurement_seen`、`reserved`。这是首版上电自检，不替代实物距离精度测试。

### 首版保留命令

`SET_CONFIG(0x0102)`、`BOOT_HOLD(0x0009)` 和 `ENTER_BOOTLOADER(0x01F0)` 当前返回 NOT_SUPPORTED。完成并实板验证 4 KiB Load Flash 引导后再启用升级命令；应用固件不得虚报升级能力。

## 5. 任意端口发现流程

主机在空闲端口依次发送 INFO。收到有效 KLQ1 响应后记录该端口的 `device_class + sensor_type + UID`，并按类型启动轮询。端口拔出、连续超时或帧错误达到阈值后清除绑定并重新发现。端口只能由一个设备服务占用，升级期间暂停该端口的普通轮询。
