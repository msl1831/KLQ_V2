# 主机语音工程USB接口 V0.1（主机0.11起，当前0.12）

正常积木Python下载继续使用host_usb_protocol_v0.1.md的BEGIN/DATA/END/RUN/STOP。这些命令用于工程诊断和观测，不必生成到用户Python，也不要求普通上位机实现语音运动开关。

沿用KLQ1帧、序号和CRC；响应payload先有4字节标准状态码，以下长度均指其后的数据。多字节数值为小端。INFO中EXT_VOICE=1才可使用。所有命令都要检查状态码，异步播放/版本请求的USB成功只表示请求受理。

| 命令 | 请求payload | 成功响应数据 | 含义 |
|---|---|---|---|
| 0x50 | 空 | 108 B | 状态快照ABI1 |
| 0x51 | 空 | 空 | 启动CI版本查询；轮询0x50确认有效记录和rx_frames增长 |
| 0x52 | uint32 ID，5000..5007 | 空 | 启动单条音效；跟踪ACK/B3/B4，不把ACK当作听音验收 |
| 0x53 | uint8，0或1 | 空 | RAM工程使能；默认0。置1要求EMPTY或READY且UI待机、无诊断/复位/关机占用，否则返回状态错误 |
| 0x54 | 空 | 空 | 取消当前语音动作并丢弃旧意图；不清除使能、不终止无关用户程序 |

USB STOP（0x09）是全局停止，能停止用户程序和运动；语音语义“停止电机”仅在语音执行门控内接受，不能用来结束RUNNING用户程序。ARM0/取消语音不会借机停止无关用户程序。关机/复位等状态仍可能拒绝命令，不能忽略错误码。

| 偏移 | 类型 | 字段 |
|---:|---|---|
| 0 | u8 | ABI=1 |
| 1..3 | 3*u8 | enabled, version_valid, awake |
| 4..6 | 3*u8 | 已读取CI版本major/minor/patch |
| 7..12 | 6*u8 | play_state, request_cmd, last_ack, last_error, last_rx_cmd, last_rx_seq |
| 13..15 | 3*u8 | active, 上次策略选定端口（1..3，0未选定）, last_reason |
| 16 | u32 | 最近原始接收semantic |
| 20..40 | 6*u32 | rx_frames, bad_frames, uart_errors, rx_overflows, tx_errors, timeouts |
| 44..56 | 4*u32 | accepted, rejected, completed, errors |
| 60..68 | 5*u16 | B0..B4累计计数 |
| 70..74 | 3*u16 | recognitions, duplicates, dropped_intents |
| 76 | u32 | play_id |
| 80..82 | 2*u16 | play_ack_count, play_done_count |
| 84..92 | 3*u32 | rx_bytes, tx_frames, last_rx_ms（主机时钟） |
| 96..98 | 3*u8 | 最近策略处理score、seq、phase |
| 99 | u8 | 保留0 |
| 100 | u32 | cancelled |
| 104 | u16 | pending_sound |
| 106 | i16 | 策略目标PWM，-1000..1000 |

version_valid仅代表已读版本记录，不是当前在线心跳。raw semantic和处理score/seq不一定属于同一帧，score为CI原始字节，不当作百分比。phase为0空闲/1待启动ACK/2运行/3待停止ACK；active不是电机引脚实测，completed是自然完成计数，cancelled是取消计数。

reason：0无拒绝、1未使能、2主机忙、3未唤醒、4版本不符、5无电机、6多电机、7驱动异常、8身份变化、9超时、10意图过期、11重复、12不支持、13取消。端口字段可能保留上次选择，设备是否在线仍读端口状态。

工程CLI：python tools/voice_usb.py --port COM123 status；实际COM必须重新枚举。监测与CLI不能同时打开串口：POST /api/pause，200且released=true后才能打开CLI；CLI关闭后POST /api/resume。等待新鲜有效INFO/语音快照确认通信。页面版本标签显示“已读版本”，不冒充CI当前在线。
