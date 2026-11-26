@page sdk_json JSON 返回契约

以下为 ABI v3 的 JSON 契约。五个接口返回 UTF-8 JSON，末尾 NUL 计入 size；空设备列表为 `[]`，不是空指针。深度帧 metadata_json_utf8 仅在回调内有效；视频帧该指针为 NULL，不是字符串 `null`。

JSON Schema（draft 2020-12）和脱敏示例随文档提供。新增字段属于兼容扩展，调用方必须忽略未知字段；删除必有字段、改变已有字段类型或含义需要新的 ABI 契约。诊断文字和 phase 可变化，勿据此硬编码业务状态。未知状态值应保留原始 JSON 并走保守的未知分支。`check_id`、`tof_timestamp_ns` 在所有取值范围内都是 JSON integer。`raw_host_dispatch_ns` 等标为十进制字符串的诊断计时字段在所有取值下都是 string。按字段的 Schema 解析，类型不会在 2^53 处切换。Python 的 `json.loads` 保留整数精度；JavaScript 应在首次解析时使用保留整数原文的解析器，或从 C 结构读取时间戳和检查 ID。先解析成双精度浮点再转 BigInt 可能已经丢失低位。

下方“必有”只表示所在对象出现时必须有；可选字段可以缺省，只有明确列出 null 的字段允许 null。Open 与 Connect 系列的 GetState 字段有差异，业务控制优先使用 GetSessionStatus/GetManagerStatus。


## 一次分配的 JSON 快照

五个查询分别提供 ListDevicesAlloc、GetStateAlloc、GetUsbStatusAlloc、ListSavedDevicesAlloc、GetDeviceManagerStateAlloc，返回格式与对应缓冲区函数完全相同。参数为句柄、char **、uint32_t *；成功容量包含末尾 NUL，失败输出设为 NULL 和 0。

~~~c
char *json = NULL;
uint32_t bytes = 0;
CxwlDepthResult result = CxwlDepth_GetStateAlloc(session, &json, &bytes);
if (result == CXWL_DEPTH_OK) {
    /* 在这里解析 json；bytes - 1 是 UTF-8 内容长度。 */
    CxwlDepth_FreeJson(json);
}
~~~

查询仍在句柄所有者线程调用，支持帧／事件回调内的只读查询。返回字符串不依附句柄，可以在关闭句柄后继续读取，并在任意线程调用 FreeJson 释放；同一指针只释放一次，不能同时读写与释放。FreeJson(NULL) 成功。不得使用 free、delete 或其他模块的释放函数；在卸载 SDK 共享库前释放全部快照。帧里的 metadata_json_utf8 仍是回调借用内存，不能传给 FreeJson。

C++ 可用 cxwl::state、cxwl::devices、cxwl::manager_state 获取 std::string，辅助函数自动释放 SDK 快照。

### 会话登记设备列表

[机器可读 Schema](schemas/ListDevices.schema.json)

| 字段 | 类型 | 必有 | 含义 |
|---|---|---|---|
| `id` | string | 是 | 设备 ID；Open 为内部登记 ID，Connect 为不透明 ID；勿解析。 |
| `name` | string | 是 | 显示名，可为空。 |
| `host` | string | 是 | 设备连接地址；对外日志须脱敏。 |
| `serial` | string | 是 | 登记序列号，可为空；须脱敏。 |
| `ready` | boolean | 是 | 设备档案的连接准备状态。实时连接状态通过 GetSessionStatus 查询。 |
| `selected` | boolean | 是 | 是否当前所选。 |
| `family` | string | 是 | 兼容的设备族标识。 |
| `modes` | array | 是 | 支持的 SDK 模式值。 |

### 管理器登记设备列表

[机器可读 Schema](schemas/ListSavedDevices.schema.json)

| 字段 | 类型 | 必有 | 含义 |
|---|---|---|---|
| `id` | string | 是 | 不透明设备 ID，供管理器及连接选项使用。 |
| `name` | string | 是 | 自定义显示名。档案使用系统默认名称时，此查询返回空字符串，档案本身不变。 |
| `selected` | boolean | 是 | 是否当前默认。 |
| `ready` | boolean | 是 | 设备档案的连接准备状态。实时连接状态通过 GetSessionStatus 查询。 |

### GetState 的 state 与 connection_state 字段

[机器可读 Schema](schemas/GetState.schema.json)

| 字段 | 类型 | 必有 | 含义 |
|---|---|---|---|
| `connection_state` | string | 是 | connecting/connected/reconnecting/error/stopping/stopped/disconnecting；所有连接入口均提供。 |
| `error` | string | 是 | Connect 系列为错误分类字符串或空；Open 会话为内部诊断文本或空。程序优先读取 GetSessionStatus.error_category。 |
| `busy` | boolean | 是 | 操作执行／退出中。 |
| `mode` | integer | 是 | SDK 常量 1／2，分别由 SDK 映射到板端预设 24／25；调用方传 1／2，见术语表。 |
| `video_available` | boolean | 是 | 当前代次视频可用。 |
| `depth_available` | boolean | 是 | 当前代次深度可用。 |
| `connection_stage` | integer | 是 | 0=IDLE，1=CHECKING，3=STARTING，4=WAITING_FOR_DEPTH；保留 2。 |
| `retaining_display` | boolean | 是 | 恢复期间可临时保留上一帧像素。 |
| `switch_active` | boolean | 是 | 正在切换采集模式。 |
| `switch_estimated_ms` | integer | 是 | 最近一次切换的预计毫秒数，仅在 switch_active 为 true 时用于进度提示。 |
| `video_fps` | number | 是 | 主机按约 1 秒窗口统计的视频帧率，清除视频后为 0。 |
| `depth_clear_reason` | string | 是 | 深度清除原因；诊断字符串，可为空。 |
| `board_files_device_id` | string | 是 | 需要修复的设备不透明 ID；无则空。 |
| `usb_error` | string / null | 否 | USB 诊断文本；没有 USB 对象时可能省略或为 null。 |
| `device_id` | string | 否 | Open 会话选中设备的登记 ID。 |
| `user_stopped` | boolean | 否 | Open 会话主动停止标志。 |
| `retry_pending` | boolean | 否 | Open 会话待重连。 |
| `board_files_required` | boolean | 否 | Open 会话要求修复板端文件。 |
| `epoch` | integer | 否 | Open 会话的展示代次；Connect 系列从结构化状态读取 session_epoch。 |
| `state` | string | 否 | Open 会话可读状态文字，不作为枚举解析。 |
| `detail` | string | 否 | Open 会话的详细状态文字。 |

`device_id/user_stopped/retry_pending/board_files_required/epoch/state/detail` 仅 Open 会话提供。其余公共字段两条路径都有；usb_error 根据 USB 对象是否存在可能缺省。Connect 系列中的 error 是空字符串或 usb_setup_failed/board_files_required/capture_failed，Open 中 error 为内部诊断文本。

### 会话 USB 状态

[机器可读 Schema](schemas/GetUsbStatus.schema.json)

| 字段 | 类型 | 必有 | 含义 |
|---|---|---|---|
| `state` | string | 是 | idle/checking/ready/network_required/update_required/registration_required/enrolling/enrolled/using_saved_device/cancelled/error。`checking`、`enrolling` 时继续派发。会话停止且空闲，检查 ID 有效且状态为 `ready`、`network_required`、`update_required` 或 `registration_required` 时可提交 `CxwlDepth_EnrollUsb`。`start_after=0` 时等登记完成再启动。检查取消或 ID 失效后重新检查。 |
| `check_id` | integer | 是 | 有效 USB 检查 ID；0 表示无有效检查。按 64 位整数读取。 |
| `phase` | string | 否 | 诊断阶段字符串；可新增。无检查对象时省略。 |
| `error` | string | 否 | 诊断文本，成功时可为空；无检查对象时省略。 |
| `device_id` | string | 否 | 会话级 USB 状态中的内部登记 ID。 |
| `runtime_replaced` | boolean | 否 | 是否替换运行文件。 |
| `attachment_id` | string | 否 | 接入指纹，不透明值。 |
| `host_authorization_id` | string | 否 | 当前 Windows 用户对这台相机的主机授权标识。登记成功后可将此标识交给设备管理员，用于单独授予维护或标定权限。 |
| `known` | boolean | 否 | 发现时是否已登记。 |
| `absent` | boolean | 否 | 是否未发现 USB。 |

### 设备管理器状态

[机器可读 Schema](schemas/GetDeviceManagerState.schema.json)

| 字段 | 类型 | 必有 | 含义 |
|---|---|---|---|
| `busy` | boolean | 是 | 内部会话／后台操作忙。 |
| `can_manage` | boolean | 是 | 无采集句柄且管理器空闲。 |
| `usb` | object | 是 | 使用上面的会话 USB 字段表，管理器省略 `device_id`。 |
| `maintenance` | object | 是 | 对象，字段见下表。 |
| `recovery` | object | 是 | 设备恢复进度与结果，字段见下表。 |

### recovery 子对象

| 字段 | 类型 | 必有 | 含义 |
|---|---|---|---|
| `state` | string | 是 | idle/running/ready/error；ready 表示恢复完成，可以重新连接。 |
| `error` | string | 否 | 错误说明；成功时为空。 |
| `exit_code` | integer | 否 | 完成后的 设备操作结果码。 |
| `faults_cleared` | boolean | 否 | 本次恢复及运行环境验证是否完成。 |
| `cleared_fault_count` | integer | 否 | 清理的故障标记数。 |
| `board_archive` | string | 否 | 板端故障证据目录。 |
| `runtime_replaced` | boolean | 否 | 是否更新了配套运行文件。 |

### maintenance 子对象

| 字段 | 类型 | 必有 | 含义 |
|---|---|---|---|
| `state` | string | 是 | idle/running/ready/error。必须检查 ready 才能判断维护成功。 |
| `exit_code` | integer | 否 | 设备操作结果码；完成后出现，-1 表示启动／异常失败。 |
| `error` | string | 否 | 错误说明；错误或完成后出现，可为空。 |
| `archived_rollbacks` | integer | 否 | 成功归档的旧副本数；完成回执中出现。 |
| `runtime_replaced` | boolean | 否 | 是否替换当前载荷；完成回执中出现。 |

## CxwlDepthFrame.metadata_json_utf8

[机器可读 Schema](schemas/FrameMetadata.schema.json)

| 字段 | 类型 | 必有 | 含义 |
|---|---|---|---|
| `width` | number | 是 | 输出深度列数。 |
| `height` | number | 是 | 输出深度行数。 |
| `depth_unit` | string | 是 | 径向米制 float32。 |
| `depth_location` | string | 是 | 原生测距。 |
| `native_order` | boolean | 是 | 保留原网格顺序。 |
| `interpolation` | boolean | 是 | 不插值。 |
| `source_generation` | string | 是 | 板端采集代次；与帧 capture_generation_utf8 对应。 |
| `generation` | integer | 是 | 主机展示代次；与 session_epoch 对应。 |
| `frame_id` | integer | 是 | 板端测量帧号。 |
| `tof_sequence` | integer | 是 | 板端深度序号。 |
| `tof_timestamp_ns` | integer | 是 | 板端单调时间，纳秒；按 64 位整数读取。 |
| `calibration_release` | string | 是 | 板端帧报告的标定发布标识，对应配套标定清单的 release_id。 |
| `sensor_mode` | integer | 是 | 传感器预设。 |
| `candidate_mode` | integer | 是 | 板端普通／高精度模式。 |
| `model` | string | 是 | 当前模式显示名。 |
| `raw_age_ms` | number | 是 | 接收路径估计的数据年龄，包含请求往返耗时，单位毫秒。 |
| `raw_board_age_ms` | number | 否 | 板端驱动完整帧发布时间到 接收端采样时刻的数据年龄，毫秒。 |
| `raw_request_roundtrip_ms` | number | 否 | 主机本次取数的完整往返时间，毫秒。 |
| `raw_host_dispatch_ns` | string | 否 | 主机入队时单调时钟纳秒，十进制字符串；仅供本进程时延诊断。 |
| `raw_callback_dispatch_ms` | number | 否 | 主机入队到 SDK 准备回调的时间，毫秒。 |
| `raw_age_lower_bound_ms` | number | 否 | 驱动完整帧发布到准备回调的时延下界。 |
| `raw_age_upper_bound_ms` | number | 否 | 同一分段时延上界，以下界加本次完整 RTT 保守估算。 |
| `dtof_fov_correction` | number | 是 | 深度处理采用的 ToF 视场校正档位，本产品为 6（F6）。传感器自身的档位见 sensor_fov_correction。 |
| `tof_reused` | boolean | 是 | 未重用旧深度。 |
| `work_mode` | string | 是 | 原生深度路径。 |
| `dtof_width` | number | 是 | 原始测距列数。 |
| `dtof_height` | number | 是 | 原始测距行数。 |
| `dtof_bytes` | number | 是 | 原始测距包字节数（每点 4 字节）。 |
| `dtof_format` | string | 是 | 板端原始数据布局。 |
| `dtof_range_unit` | string | 是 | 原始测距量化单位；输出图像已换成米。 |
| `dtof_source` | string | 是 | 原始测距来源标识。 |
| `dtof_coordinate` | string | 是 | 原生径向坐标。 |
| `tof_filter_raw_preserved` | boolean | 是 | 原始测距保留。 |
| `pairing_policy` | string | 是 | 深度和视频的输出方式；independent_streams 表示分别输出。 |
| `sensor_fov_correction` | integer | 否 | 板端回执的传感器 FOV 设定；有标定回执时出现。 |
| `processing_fov_correction` | number | 否 | 标定回执中的处理档位，与 dtof_fov_correction 对应，本产品为 6。 |
| `temporary_fov_calibration` | boolean | 否 | 是否临时覆盖；有标定回执时出现。 |
| `tof_calibration` | object | 否 | 对象，字段见下表。 |

### tof_calibration 子对象

| 字段 | 类型 | 必有 | 含义 |
|---|---|---|---|
| `schema` | number | 是 | 板端标定回执版本。 |
| `run_id` | string | 是 | dtof_ 加板端采集代次。 |
| `sensor_fov_correction` | integer | 是 | 传感器 FOV 设定。 |
| `processing_fov_correction` | number | 是 | 标定回执中的处理视场校正档位，6 对应 F6。 |
| `temporary_calibration` | boolean | 是 | 传感器 FOV 与处理 FOV 是否不同。 |

分段时延统计区间为板端完整帧发布至 SDK 准备回调。其他底层传输附带的 boot_id、protocol、raw_*、video_* 等字段保留为扩展诊断，不属于应用控制契约。生产日志应对设备／接入身份脱敏。

## 脱敏示例

[samples.json](schemas/samples.json) 包含所有接口及 Open/Connect 两条状态路径。设备 ID、地址和代次为合成值；字段与实现一致，不表示一次实际硬件测试。

@verbinclude samples.json
