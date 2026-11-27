# CXWL Depth Camera SDK 3.0.0 开发版 · Windows x64

本地 USB 直连构建使用 SDK 3.0.0、ABI v3，配套预装 `1.0.0-preview.2-usb-direct` 服务的相机。

C/C++ 共用一个 DLL、导入库和 ABI v3。C++ 可使用 DepthSdk.hpp 的辅助类型。SDK 3.0.0 提供 46 个公开函数以及 8×8、16×16、48×32 米制深度和原视频回调。每个进程使用一个采集会话。调用 `CxwlDepth_Connect` 或 `CxwlDepth_SetMode` 时传入相应 SDK 模式常量。 [六档模式值与厂商测距范围](sdk/specifications.md#原生深度档位与测距范围) · [API 参考](DEPTH_SDK_API_REFERENCE.md)

## 连接与断开

重新插入相机后，Windows 有时会先列出仍在执行重复地址检测的 RNDIS IPv4 地址。若此时无法绑定该地址，SDK 会在本次连接内等候最多 5 秒，待同一接口的地址可用后继续连接。

首次接入或快速验证可使用 `CxwlDepth_Connect`。应用需要指定设备或连接策略时使用 `CxwlDepth_ConnectWithOptions`。需要先配置会话、等待操作员或业务流程启动采集时使用 `CxwlDepth_Open`。[三种方式的使用场景与调用顺序](DEPTH_SDK_API_REFERENCE.md#选择连接方式)

### 使用默认方式连接

1. 通过 USB 数据线接入预装设备服务的 CXWL Depth Camera。调用 `CxwlDepth_Connect(mode, frame_callback, event_callback, user, &session)`。SDK 识别相机的 USB 网卡，核验身份并按需登记，采集使用该机的 F6 标定。
2. `CXWL_DEPTH_OK`（0）表示连接请求已受理。独立 C/C++ 程序在所有者线程上用 `CxwlDepth_WaitForEvent` 派发事件，Qt 程序使用主线程事件循环。收到 `CXWL_DEPTH_EVENT_STATE_CHANGED` 后用 `CxwlDepth_GetSessionStatus` 查询进度。状态达到 `CXWL_DEPTH_STATE_STREAMING` 时，深度和视频均可用。
3. 调用 `CxwlDepth_Disconnect(session)` 停止采集并释放会话。返回 `CXWL_DEPTH_BUSY`（2）时，会话还在停止处理中，继续派发事件并在同一线程重试。返回 `CXWL_DEPTH_OK`（0）即确认 SDK 会话已释放，此时句柄失效。主机断开收流后，板端按自身清理流程退出。

桌面示例在等待采集就绪时显示「正在启动采集」和不定进度条，深度与视频都就绪后显示画面。点击停止会先显示「正在停止」，SDK 返回 `CXWL_DEPTH_OK` 后显示「已停止采集」。

设备连接信息由 SDK 在当前 Windows 用户目录下管理。安装版上位机与 SDK 共用该用户的登记记录和 Windows 凭据管理器。首次 USB 接入需要 Windows 识别相机的 RNDIS 网卡。相机固定使用 `172.32.0.93/16`。初次配置网卡时，在 SDK 包根目录的管理员 PowerShell 中运行 `./Script/configure_cxwl_depth_camera_usb.ps1 -Serial (Read-Host '机身上的 16 位 USB 序号')`，为指定相机的主机 USB 网卡配置同网段地址。已有有效地址时直接连接。

两台相机可以同时插在 Windows 上。首次登记按机身序号核对目标，之后使用设备管理示例的 `connect-saved ID` 连接指定相机。两个进程各传一个不同的登记 ID 可并行采集。 [双机登记与按 ID 选机](sdk/usb.md)

可选的状态回调报告状态变化和帧失效。对 `CxwlDepth_Connect` 返回的会话调用 `CxwlDepth_GetState`，可读取 `connecting`、`connected`、`reconnecting`、`error` 或 `disconnecting`。收到 `error` 时，通过 `error` 字段区分 USB 准备失败（`usb_setup_failed`）、板端文件待修复（`board_files_required`）或采集失败（`capture_failed`）。USB 准备或板端文件失败时可检查 USB 连接后重试。状态 JSON 长度包含结尾的 NUL，可先传空缓冲区获取所需长度。

## 帧

会话在所有者线程上调用。回调期间可查询状态，控制操作在回调返回后执行。帧、字符串和图像地址仅在回调期间有效。跨线程或异步处理时先复制所需数据。

| 流 | 数据 |
| --- | --- |
| 原视频 | 2304×1296 RGB8。使用帧给出的行跨度和主机单调时间戳。 |
| 板端普通和高精度 | 8×8、16×16 或 48×32 小端 float32 米制径向距离。按帧给出的宽、高和行跨度读取，使用板端单调时间戳。无效值为 0。 |

原视频和深度共享会话代次。各档均输出原始测距，元数据中的 `interpolation` 为 `false`。`CxwlDepth_SubscribeEx` 的 SNR 和有效位图与该深度帧尺寸相同。应用在切档或会话代次变化时按新帧尺寸重建缓冲区。

在深度回调中调用 `CxwlDepth_GetFramePointCloud` 读取板端同帧 XYZ。需要异步保存时按行复制 XYZ 图像和帧元数据。仅有径向深度副本无法导出点云。

## 桌面示例与可选设备管理

接入其他项目时配套使用本构建的头文件、导入库及完整运行时。实时深度回调可通过 `CxwlDepth_GetFramePointCloud` 取得板端有序 XYZ，需要保存文件时调用 `CxwlDepth_SavePointCloudPly`。Viewer 点云工具栏支持开始录制与停止保存，每帧对应一个二进制 PLY。可独立选择随录同帧径向深度和 RGB 图像。 [客户录制数据读取说明](POINT_CLOUD_RECORDING.md) [文件格式及坐标](sdk/data.md#将深度帧导出为-ply)

仓库中的 Cxwl Depth Camera 展示了 Qt 程序如何使用同一个 C SDK 接入深度图、点云和原视频。SDK 调用与回调转发集中在 `demo/DepthSdkClient.cpp`。只接收帧的程序可直接使用上面的连接与断开流程。

| 操作 | 公开接口 |
| --- | --- |
| 打开、关闭设备管理 | `CxwlDepth_OpenDeviceManager`、`CxwlDepth_CloseDeviceManager` |
| 列表、切换、移除 | `CxwlDepth_ListSavedDevices`、`CxwlDepth_SetActiveDevice`、`CxwlDepth_RemoveSavedDevice` |
| 检查 USB、读取进度和确认结果 | `CxwlDepth_ScanUsb`、`CxwlDepth_GetDeviceManagerState` |
| 指定下次连接目标 | `CxwlDepth_SetConnectionTarget`，随后调用 `CxwlDepth_Connect` |
| 运行期间切换分辨率或精度档位 | `CxwlDepth_SetMode` |
| 在现有会话内立即重连 | `CxwlDepth_Reconnect` |
| 检查、修复与归档板端文件 | `CxwlDepth_MaintainBoardFiles` |
| 清理故障状态并恢复设备 | `CxwlDepth_RecoverDevice`，完成后重新连接 |

设备管理与采集使用同一所有者线程。传入空选项时使用默认用户档案。列表返回 `id`、`name`、`selected`、`ready`，其中 `id` 可传入设备管理接口。停止并断开会话后可切换设备、扫描 USB 或维护文件。带有设备管理器的独立程序可在同一线程使用 `CxwlDepth_PollDeviceManager` 派发事件。关闭时先断开采集，再关闭设备管理器；收到 `CXWL_DEPTH_BUSY` 时继续派发事件并重试。

USB 检查结果位于状态 JSON 的 `usb` 对象。检查本身只读。使用 `CXWL_DEPTH_TARGET_USB` 时，可传入 `usb.check_id` 将下一次连接绑定到已检查的 USB 设备。SDK 在登记或启动前重新核对 USB 身份与设备库。使用 `CXWL_DEPTH_TARGET_SAVED` 时，SDK 连接当前已登记设备。板端维护进度位于 `maintenance` 对象，通过 `CXWL_DEPTH_EVENT_MAINTENANCE_CHANGED` 通知。修复模式恢复采集必需文件，维护模式还将旧回滚副本归档到本机。

## 使用 Windows SDK 包

解压后，双击 `Example/bin/CxwlDepthViewer.exe` 可运行图形示例。集成自己的应用时，包含 `SDK/include/DepthSdk.h`，链接 `SDK/lib/CxwlDepthSdk.lib`。

将 SDK 的 `SDK/bin` 内容部署到 Windows x64 程序可加载的目录。在 SDK 根目录运行 C 示例，默认采集普通模式。示例收到视频和深度后断开。

```powershell
./Example/bin/CxwlDepthQuickStart.exe
./Example/bin/CxwlDepthQuickStart.exe high
```

使用 VS 2022 x64 工具链编译运行包内的示例。

```powershell
cmake -S Example -B ../cxwl-example-build -G "Visual Studio 17 2022" -A x64
cmake --build ../cxwl-example-build --config Release
```

## 会话控制与数据读取

调用 `CxwlDepth_Stop` 可停采并保留会话。通过 `CxwlDepth_GetSessionStatus` 确认状态达到 `CXWL_DEPTH_STATE_STOPPED` 后，可对已就绪的设备再次调用 `CxwlDepth_Start`。`CxwlDepth_Close` 和 `CxwlDepth_Disconnect` 都能停采并释放会话，返回 `CXWL_DEPTH_BUSY` 时继续派发事件并重试。

包含 size 字段的输入选项和输出结构体须先清零，再设置 size=sizeof(结构体类型)；连接选项的 abi_version 设置为 CXWL_DEPTH_ABI_VERSION。业务状态判断使用 GetSessionStatus／GetManagerStatus，详细诊断通过 GetState／GetDeviceManagerState 的 JSON 输出读取。SubscribeEx 提供同帧的 SNR／valid 图，与 Subscribe 互相替换；帧数据仍只有回调有效期。

## 配套文档

双击包内 `Doc/api/Chinese/index.html` 可离线查询接口。板端标定版本可通过 `CxwlDepth_GetDeviceInfo` 读取。RGB 校正步骤见[独立工具说明](sdk/rgb_lsc.md)。USB 接入说明和诊断脚本随包提供。[测量规格](sdk/specifications.md) · [JSON 字段](sdk/json.md) · [错误处理](sdk/errors.md) · [示例](sdk/examples.md)

维护者从源码制作 SDK 包时可查阅 [源码与构建材料](SOURCE_DISTRIBUTION.md)。

## ABI v3 与安装集成

旧版客户端须使用 ABI v3 头文件、导入库及配套 DLL 重新编译。[迁移说明](sdk/lifecycle.md#abi-v3-迁移)

SDK/bin 集中保存运行依赖，Example/bin 包含八个启动器。部署可选择 MINIMAL（查询／保存已有 XYZ）、STANDARD（完整 C/C++ 采集，默认）或 FULL（含 Viewer 界面依赖）。安装工程提供 install(EXPORT)，标准配置目录为 share/CxwlDepthSdk。vcpkg 和 Conan 配方位于 Integration。 [详细构建步骤](sdk/build.md)
