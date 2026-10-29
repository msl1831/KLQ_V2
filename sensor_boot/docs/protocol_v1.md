# 统一 UART 引导协议 v1

2026-10-29，端点为 PY32F002BF15U6TR 的 USART1，PA3 TX / PA4 RX，115200 8N1。引导版本 `0x00010000`。所有数值小端；不使用主控 USB 引导的镜像起点或页长。

## 身份与内存

| 板型 | device_class | board_id | hw_compat | INFO +8 |
| --- | ---: | ---: | ---: | ---: |
| 超声波 ul | 1 | 1 | 1 | sensor_type=1 |
| 双路红外 ir | 1 | 3 | 1 | sensor_type=2 |
| PY32 电机 motor | 2 | 2 | 2 | hw_revision=2 |

共用引导的 INFO 数据为 36 B，沿用现有设备 INFO：`protocol:u16=1, mode:u8=0, class:u8, board:u16, hw:u16, type_or_revision:u16, reserved:u16=0, boot_version:u32, capabilities:u32=8, max_payload:u16=132, app_capacity:u16=18304, UID:3*u32`。配套应用 mode=1，UL/IR capabilities=15、电机=9，其中 bit3 表示具有升级入口。应用版本沿用 UL 1.0.1、IR 1.0.0、电机 2.0.0，启动布局由能力位、身份描述及 GET_LAYOUT 区分。

应用起点 `0x08001800`，上限 18304 B，元数据页 `0x08005F80`，Flash 页 128 B。应用内 offset `0xC0` 必须含 `<4sHHII>`：`KID1, board, hw, 0x08001800, abi=1`；前 192 B 为 PY32 向量表，代码从 `0x080018D0` 起。镜像最小 208 B、长度须偶数。初始 SP 要求 8 字节对齐且在 `(0x20000000,0x20000C00]`；复位入口必须为镜像范围内的 Thumb 地址。

## 帧

16 B 头为 `magic[4]="KLQ1", sequence:u32, command:u16, payload_length:u16, crc32:u32`。CRC32/IEEE：多项式反射值 `0xEDB88320`，初值/末异或 `0xFFFFFFFF`，覆盖头前 12 B 拼接 payload，排除 CRC 字段。测试向量 `123456789 → 0xCBF43926`。

响应 command 为请求 command OR `0x8000`，sequence 原样返回；响应 payload 首先是 `status:u32`，后跟命令数据。状态 0 成功、1 未知命令、2 参数或长度错误、4 当前无活动升级、6 擦写/读回/无有效镜像等错误。CRC 错误帧丢弃；头声明 payload 超过 132 B、UART 错误或帧字节间隔超过 20 ms 时丢弃当前帧，主机超时重发。

## 命令

以下响应数据均已排除 status 的 4 B。

| 命令 | 编号 | 请求数据 | 成功响应数据 |
| --- | --- | --- | --- |
| INFO | `0x0001` | 空 | 36 B 身份 |
| BOOT_HOLD | `0x0009` | 空 | 空，保持引导直到 RUN/复位 |
| ENTER_BOOTLOADER | `0x01F0` | 空 | 引导中等同 HOLD；应用等 TX 完成再复位，电机先停机、红外先关发射 |
| BEGIN | `0x0200` | board:u16, hw:u16, length:u16, crc32:u32 | next_offset:u16 |
| DATA | `0x0201` | offset:u16, bytes[2..128] | next_offset:u16 |
| END | `0x0202` | 空 | 空 |
| RUN | `0x0203` | 空 | 空，请求发送应答后跳转；发送/TC超时路径的完成保证仍待处理和板测 |
| GET_LAYOUT | `0x0204` | 空 | app_base:u32, capacity:u16, page:u16, metadata:u32, boot_size:u32，共16 B |

BEGIN 先核对板型、兼容号和长度，成功后先擦有效记录再擦必要应用页。活动升级中的同 length/CRC BEGIN 返回当前 offset，不重新擦除。成功 END 后重新 BEGIN 则开始一次新升级。错误应答不可作为推进 offset 的依据。

DATA 的 offset 必须页对齐，只允许下一个顺序页或已完成页的完全相同重传。每包 128 B，末包可为不足 128 B 的偶数长度；不允许部分覆盖旧页。重传读回不一致、页写入/读回失败会使本次事务失效，需重新 BEGIN。END 在 offset=length 后校验栈/入口、应用内身份和完整 CRC，再提交有效记录；重复成功 END 幂等。END 读失败可重试，若持续失败应重新 BEGIN。

有效记录前 16 B 为 `<HHHHII>`：`board, hw, length, reserved=0, image_crc32, magic=0x314D4C4B`，其余页填 FF。启动独立校验记录、镜像身份、向量及 CRC。断电后事务 RAM 状态丢失，无有效记录就保持引导；重新 BEGIN 从头写。

## 主机流程

1. 本地验证升级容器，读取端口 INFO，匹配板型/兼容号/class，记录 UID。
2. 应用模式发送 ENTER_BOOTLOADER，然后在复位后的 500 ms 窗口内发送 HOLD。进入引导前的 ACK 丢失可容忍，继续尝试 HOLD；窗口错过时重新读 INFO/进入引导。
3. 再读 INFO，要求 mode=0 且 UID 未变；读 GET_LAYOUT，确认分区。历史电机独立引导没有 GET_LAYOUT，当前工具会在 BEGIN 前停止。
4. BEGIN，按返回的合法页 offset 发 DATA；每页等待 ACK，超时重发同一页。相同上电会话可续传，重启后应接受 offset=0 并从头下载。
5. END；成功后 RUN，然后等待同 UID、mode=1 的应用 INFO。RUN ACK 丢失时继续查询应用 INFO。

建议 HOLD/进入引导超时 150 ms、BEGIN 超时 10 s、其他命令 500 ms，最多 3 次重试。帧长最多 148 B，主机只需一个分包缓冲。

科乐机主控已接入端口独占升级状态机和USB分包入口：目标端口暂停普通轮询，电机先STOP，主控自动衔接ENTER/HOLD；选择设备后锁定UID，进入引导后再次核对INFO及布局。PC按页流式传送，主控只缓存一个请求和响应。外层USB会话、重传、取消及工具用法见 [usb_bridge_v1.md](usb_bridge_v1.md)。

## 容器

`*_update.klq` 的 20 B 头为 `<4sHHHHII>`：`KSU1, format=1, board, hw, image_length, image_crc32, app_base`，随后恰好 image_length 字节原始应用。头不写进 Flash。打包器先核对应用自身的 KID1 描述再生成容器；引导 END 也会独立核对。工厂 HEX/BIN 含引导及有效记录，不能作为 UART DATA 内容。
