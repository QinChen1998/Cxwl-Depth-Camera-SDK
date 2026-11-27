@page sdk_lifecycle 会话与调用规则

@anchor sdk_connection_modes

## 选择连接方式

每个进程最多使用一个采集会话。每次接入选择一个入口即可。

| 入口 | 适合的场景 | 调用顺序 |
|---|---|---|
| `CxwlDepth_Connect` | 首次接入、运行 QuickStart 或快速确认深度和视频可用。SDK 按默认策略选择设备，按需完成 USB 登记并启动采集。 | `CxwlDepth_Connect` → 接收帧 → `CxwlDepth_Disconnect` |
| `CxwlDepth_ConnectWithOptions` | 应用已选中一台相机、要连接刚检查过的 USB 设备，或现场只允许连接已登记设备。应用明确指定目标和准备策略，SDK 负责连接并启动。 | 填写 `CxwlDepthConnectOptions` → `CxwlDepth_ConnectWithOptions` → 接收帧 → `CxwlDepth_Disconnect` |
| `CxwlDepth_Open` | 应用需要在采集前配置会话，并自行控制启动时机。例如产线检测软件先选相机和档位，等操作员点击「开始检测」再启动。 | `CxwlDepth_Open` → `CxwlDepth_Subscribe` 或 `CxwlDepth_SubscribeEx` → 按需选择设备及档位 → `CxwlDepth_Start` → 接收帧 → `CxwlDepth_Close` |

`CxwlDepth_Open` 通过 `CxwlDepthOptions` 接受运行目录和设备档案的绝对路径。创建后会话处于停止状态，可通过 `CxwlDepth_ListDevicesAlloc` 取得已登记设备的 ID，再用 `CxwlDepth_SelectDevice` 选设备，用 `CxwlDepth_SetMode` 选模式。首次 USB 登记可在会话空闲时调用 `CxwlDepth_CheckUsb`，派发事件并通过 `CxwlDepth_GetUsbStatusAlloc` 取得有效的 `check_id`，随后调用 `CxwlDepth_EnrollUsb`，将 `start_after` 设为 0。登记完成后即可配置并调用 `CxwlDepth_Start`。

`CxwlDepth_Connect`、`CxwlDepth_ConnectWithOptions` 和 `CxwlDepth_Start` 返回 `CXWL_DEPTH_OK` 表示启动请求已受理。调用 `CxwlDepth_GetSessionStatus` 查询进度，状态达到 `CXWL_DEPTH_STATE_STREAMING` 时，视频和深度均已就绪。连接时未传帧回调的应用应随即调用 `CxwlDepth_Subscribe` 或 `CxwlDepth_SubscribeEx`。SDK 只派发订阅后的帧。需要先完成订阅再启动时，可使用 `CxwlDepth_Open`。

所有句柄操作在创建线程调用。独立 C/C++ 程序使用 `CxwlDepth_WaitForEvent` 或 `CxwlDepth_Poll` 派发会话事件，Qt 程序使用主线程事件循环。需要在回调结束后使用帧数据时，在回调内逐行复制要保留的图像和质量图，复制需要的设备标识、采集代次和元数据字符串。

### 停止与释放

需要保留会话以便稍后继续采集时，调用 `CxwlDepth_Stop`。该调用会取消尚未执行的启动请求。等状态达到 `CXWL_DEPTH_STATE_STOPPED` 后，已准备就绪的设备可再次调用 `CxwlDepth_Start`。设备尚未准备时，`CxwlDepth_Start` 返回 `CXWL_DEPTH_DEVICE_NOT_FOUND`。

结束使用时，`CxwlDepth_Close` 和 `CxwlDepth_Disconnect` 都能停止采集并释放会话。对同一句柄选择一个释放入口。返回 `CXWL_DEPTH_BUSY` 时，保留句柄和回调上下文，继续派发事件并在创建会话的线程重试。返回 `CXWL_DEPTH_OK` 后立即清空所存句柄，同一旧句柄不得再次传入 SDK。

### 等待会话回调

`CxwlDepth_WaitForEvent(session, timeout_ms, &dispatched)` 使用当前所有者线程派发帧与事件。返回 OK 且 `dispatched == 1` 表示执行了至少一次会话回调。返回 OK 且 `dispatched == 0` 表示时限内无回调，应用仍可检查状态或决定退出。超时 0 仅处理就绪事件。正数限制空闲等待，已经开始的回调会在返回前执行完毕。调用前先注册所需回调，回调期间继续遵守只读查询规则。管理器使用 `CxwlDepth_PollDeviceManager`，Qt 应用使用自己的事件循环。

## 目标和策略

`CxwlDepth_ConnectWithOptions` 的选项仅作用于本次请求。指定一台已登记设备时，将 `target` 设为 `CXWL_DEPTH_TARGET_SAVED`，将 `device_id_utf8` 设为登记列表的 `id`。只使用已登记设备时选用 `CXWL_DEPTH_CONNECT_SAVED_ONLY`。需要在接入时核验预装服务并登记时选用 `CXWL_DEPTH_CONNECT_PREPARE`。连接刚检查过的 USB 相机时，将 `target` 设为 `CXWL_DEPTH_TARGET_USB` 并传入同一管理器签发的有效 `usb_check_id`。SDK 在登记前再次核对实物。

应用已打开设备管理器时，通过 `CxwlDepth_Connect` 或 `CxwlDepth_ConnectWithOptions` 创建采集会话。`CxwlDepth_ConnectWithOptions` 此时沿用管理器的路径，两个路径选项须为 `NULL`。`CxwlDepth_SetConnectionTarget` 为下一次 `CxwlDepth_Connect` 指定一次性目标。`Connect` 返回 OK 即消耗该目标，即使后续异步启动失败。同步拒绝时仍保留目标。显式 `ConnectWithOptions` 只使用传入的选项，保留管理器待用目标。`CxwlDepth_SelectDevice` 接受登记列表返回的 ID，并在会话停止且空闲时切换设备。

## 所有者线程与回调

每个进程最多一个管理器和一个采集会话。管理器内部会话可由 Connect 系列借用；这不会产生第二个采集会话。独立 C/C++ 程序无需创建 Qt 对象，可选择主线程或固定的工作线程拥有整个会话；已有 Qt 应用必须在其事件循环线程创建句柄。独立 C 程序可调用 `CxwlDepth_PollDeviceManager` 派发管理器事件。

帧／事件回调允许只读查询，例如 `CxwlDepth_GetSessionStatus`、`CxwlDepth_GetDeviceInfo`、`CxwlDepth_GetStateAlloc`，管理器回调可用 `CxwlDepth_GetManagerStatus`，禁止控制、重新订阅和关闭。这些操作返回 BUSY。把后续控制安排到回调返回以后。日志回调不同：在日志回调中只复制日志。重入受保护入口返回 `CXWL_DEPTH_BUSY`。`CxwlDepth_AbiVersion` 直接返回版本，`CxwlDepth_GetLastError` 直接复制线程错误记录。日志注册与注销必须和其他线程调用串行。有现存句柄时，可在所有者线程、帧／事件回调外更改日志回调。

## 状态与错误

GetSessionStatus 对各入口创建的会话返回同一结构，状态枚举为 STOPPED、CONNECTING、STREAMING、RECOVERING、STOPPING、ERROR。GetManagerStatus 提供 busy、can_manage、maintenance_running 和 usb_check_id。

GetLastError 查询本线程最近同步失败的函数、错误码和说明，成功调用不会擦除。GetStateAlloc／GetDeviceManagerStateAlloc 提供 USB、维护和底层详细诊断。异步失败通过事件与结构化状态报告，连接结果可用 GetSessionStatus 确认。

## 结构体初始化与输出缓冲区

对包含 `size` 字段的输入选项和输出结构体，先清零，再将 `size` 设置为 `sizeof(结构体类型)`。连接选项中的 `abi_version` 设置为 `CXWL_DEPTH_ABI_VERSION`；该值用于检查头文件与运行库的接口约定。

例如，查询会话状态时：

```c
CxwlDepthSessionStatus status = {0};
status.size = sizeof(status);
CxwlDepthResult result = CxwlDepth_GetSessionStatus(session, &status);
```

SDK 填写的帧按 `frame->size >= offsetof(CxwlDepthFrame, image) + sizeof(frame->image)` 这类条件检查成员是否存在。

使用 Alloc 版本一次取得 JSON 快照，再调用 FreeJson 释放。2.0.0 引入五个分配型查询，自 2.2.0 弃用对应的缓冲区接口。现有应用仍可调用这五个缓冲区接口。空缓冲区取得含 NUL 的容量后再分配读取；遇到 BUFFER_TOO_SMALL 时按返回容量重试。业务状态判断使用 GetSessionStatus／GetManagerStatus。

## 订阅与设备管理

Subscribe 提供图像帧，SubscribeEx 还提供深度帧对应的 SNR 和有效位。两者互相替换订阅，每次仅使用一种帧回调。设备管理操作要求先释放采集句柄；单独 Stop 后句柄仍存在，不能进行板端维护。

## C++17 拥有型辅助类

`cxwl::Session` / `cxwl::Manager` 在创建线程拥有句柄，`get()` 返回供 C API 借用的句柄。构造前准备好回调上下文，并使回调上下文活到句柄关闭成功。拥有者固定在创建线程，禁止复制和移动。独立程序在该线程析构时派发事件直到关闭完成。先关闭采集会话，再关闭借出该会话的管理器。Qt 应用关闭返回 BUSY 时保留拥有者对象，由事件循环继续派发并重试 `try_close()`。Qt 窗口收到关闭请求时调用 `try_close()`，返回 false 则暂缓销毁窗口，在下一轮定时器或事件循环重试，返回 true 后再销毁拥有者和回调上下文。析构必须在所有者线程、回调之外执行，遇到无法安全完成的关闭会终止进程。

`cxwl::Error::code` 是本次调用结果，`info` 保留线程最近的 `CxwlDepthErrorInfo`。两个错误码不同时，异常文本会标明前一次错误的来源。`cxwl::check(result, session)` 在可查询会话时额外保存 `session_status`，其中包含状态、错误类别和连接阶段。`DepthFrame` 和 `VideoFrame` 拥有回调帧的图像及字符串副本。`Image` 接受 data 为空且宽高均为 0 的空图像。`project()` 接受 8×8、16×16 或 48×32 米制径向深度图。

## ABI v3 迁移

从 ABI v2 迁移到 ABI v3 时，需要用本版头文件重新编译客户程序。CxwlDepthCapabilities 的模式数组扩为六项，结构为 72 字节。先比较 CxwlDepth_AbiVersion() 与头文件宏，再创建句柄。RGB 校正改用独立工具。需要点云时，在深度回调中使用 CxwlDepth_GetFramePointCloud，跨回调使用前复制 XYZ。查询板端标定版本使用 CxwlDepth_GetDeviceInfo。
