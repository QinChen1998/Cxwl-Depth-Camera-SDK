# RGBD 深度相机 SDK v3.0.0 · Windows 设备服务预览 {#sdk_home}

Windows x64 C/C++ SDK，版本 3.0.0。应用通过同一会话获取 8×8、16×16 或 48×32 径向米制深度和 2304×1296 RGB8 视频。三种分辨率均提供普通和高精度档。

本预览适用于预装设备服务 1.0.0-preview.1 并配备本机 F6 标定的相机。

C/C++ 使用同一个 DLL，调用方无需 Qt 开发环境。Qt Viewer 示例使用 Qt Widgets 绘制。

## 开始使用

1. 阅读 @ref sdk_build ，准备 USB 接入及运行依赖。
2. 无相机运行 `CxwlDepthSelfTest.exe` 检查 DLL、六档能力和公开接口。连接相机后运行 `CxwlDepthPointCloud.exe -Out <已有目录>` 保存 PLY 和深度图。
3. 接入相机后运行 `Example/bin/CxwlDepthViewer.exe`，或编译 `Example/c/QuickStart`。

## 选择连接方式

- **首次接入或快速验证**用 `CxwlDepth_Connect`，按默认策略自动启动。下方最小采集示例采用此方式。
- **按指定条件连接并启动**用 `CxwlDepth_ConnectWithOptions`。例如上位机已选中一台相机，或现场只允许连接已登记的设备。
- **先配置、按需启动**用 `CxwlDepth_Open`。例如检测软件先选设备和采集模式，等操作员点击开始后再调用 `CxwlDepth_Start`。

三种入口的调用顺序和结束方式见 @ref sdk_connection_modes "连接方式与适用场景"。

## 文档导航

单文件 PDF 手册与 Markdown 参考手册均从同一头文件和教程生成。 <a href="API_REFERENCE.pdf">PDF</a> · <a href="API_REFERENCE.md">Markdown</a>

- @ref sdk_lifecycle ：连接方式、停止与释放、线程、状态与错误处理。
- @ref sdk_data ：数据格式、质量图、时间戳和坐标模型。@ref sdk_recorded_ply "读取录制的 PLY 文件" 包含文件头说明和 Python 示例。
- @ref sdk_build ：目录、CMake、QtViewer 和运行部署。
- @ref sdk_usb ：USB、板端材料与驱动。
- @ref sdk_rgb_lsc "Windows 独立 RGB 校正工具"。
- @ref DepthSdkVersion.h "DepthSdkVersion.h" 提供版本与 ABI 宏。
- @ref DepthSdk.h "DepthSdk.h" 提供函数、结构体、枚举取值、宏定义和回调类型的参考。

- @ref sdk_specifications 包含六档原生深度的模式值与厂商测距范围。
- <a href="deprecated.html">已弃用的缓冲区接口及替代函数</a>。升级步骤见 @ref sdk_lifecycle 。
- @ref sdk_json ：JSON 字段、Schema 和脱敏样例。
- @ref sdk_errors ：错误处置和状态转换。
- @ref sdk_terminology ：术语、中英对应与模式编号映射。
- @ref sdk_examples ：C/C++ 示例、USB、重连、存盘和线程。

## 主要功能

| 功能 | 说明 |
|---|---|
| 设备连接与采集 | 自动连接或指定设备，启动、停止及恢复采集 |
| 图像与测量质量 | 获取视频、米制深度、同帧 SNR 和有效位 |
| 点云与坐标模型 | 读取板端同帧 XYZ，复制点云并保存 PLY |
| 设备信息与诊断 | 查询设备信息、支持能力、运行状态、错误及日志 |
| 设备管理与维护 | 管理登记设备、检查 USB 接入和维护板端文件 |

## 最小采集示例

在包根目录运行 `cmake -S Example -B ../cxwl-build` 即可配置示例。单独编译 QuickStart 时，把 `SDK/include` 和 `Example/common` 加入包含目录并链接 `SDK/lib/CxwlDepthSdk.lib`。`SampleSupport.h` 来自 `Example/common`。每轮 `sample_poll()` 调用 `CxwlDepth_WaitForEvent`，让所有者线程派发视频和深度回调。

@include DepthSdkExample.c
