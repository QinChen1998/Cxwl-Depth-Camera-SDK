# CXWL 深度相机 SDK Viewer

适用于 Windows x64 USB 直连预览，SDK 3.0.0 / ABI v3。将预装 `1.0.0-preview.2-usb-direct` 服务的相机接入 USB，在「设备管理」中登记或选择相机。一个 Viewer 进程使用一个深度／RGB 采集会话。 [SDK 调用源码](demo/DepthSdkClient.cpp) · [双机接入步骤](../README.md#在两台相机中选择目标)

## 窗口与操作

| 区域／控件 | 操作与含义 |
|---|---|
| 左上深度 | 可选择「48×32 普通」或「48×32 高精度」。黑色表示缺测。支持保存原生网格快照和独立 ToF 显示角度。 |
| 左下视频 | 原始 RGB 视频；视频旋转不改变深度与标定。 |
| 右侧点云 | 点状／网格、深度遮挡、颜色插值、坐标轴；颜色对应 ToF Z 分量。 |
| 录制点云 | 首帧到达后点击「录制点云」并选择目录。点击「停止录制并保存」后可查看帧数与路径。每个 PLY 保存一帧传感器坐标系的 XYZ 米值，文件名按接收顺序编号。录制前可独立勾选「同时录制深度」与「同时录制 RGB」，保存同帧米制深度和独立的 JPEG 序列。随 RGB 生成时间索引。 |
| 鼠标 | 左键拖动旋转，中键拖动平移，滚轮缩放；“重置”恢复视角。 |
| 色标 | 深度和点云各自选择固定范围、动态或直方图均衡。 |
| 输入角度 | 绕 ToF 前向轴顺时针旋转，0–359°；仅作用于点云显示。 |
| 设备管理 | USB 接入、选择登记设备、修复／恢复；维护前先停止并释放采集。 |
| 复制诊断 | 状态栏按钮将 SDK 版本、ABI、设备 ID、连接状态和最近一次采集错误写入剪贴板，便于粘贴到支持请求。 |

SDK 应用可通过 `CxwlDepth_Connect` 或 `CxwlDepth_SetMode` 使用 8×8、16×16、48×32 的普通和高精度档。Viewer 启动时会将以前保存的 8×8 或 16×16 选项转为相同精度的 48×32 档。

以下截图为离线合成深度，视频区无相机输入，不代表实机测量。

![点状模式与窗口布局](images/viewer-points.png)

![网格模式](images/viewer-mesh.png)

## 构建

需要 VS 2022 v143 x64、Windows SDK 10.0.22621.0、CMake 3.21+、Qt 6.11.1 MSVC x64 的 Core、Gui、Widgets、Network。点云采用软件光栅，无需 Concurrent、Quick 3D、QML 或 OpenGL 回退库。

在包根目录执行：

```powershell
$qtRoot = Read-Host '请输入 Qt 6.11.1 MSVC x64 的安装目录'
powershell -File Script/build-examples.ps1 -BuildDirectory ../cxwl-viewer-build -WithViewer -QtRoot $qtRoot -RunOfflineTests
../cxwl-viewer-build/bin/FULL/Release/CxwlDepthViewer.exe
```

运行预编译 Viewer 不需要开发环境。界面偏好保存在 `%LOCALAPPDATA%/CXWL/DepthSdkViewer/`；设备档案由 SDK 管理，与其他 SDK 程序共用既有登记。独立 main.cpp 只负责 Qt 初始化、翻译、偏好与主窗口，没有工程验证开关或实机测试入口。

自有示例源码适用 [MIT](../LICENSE)，第三方按各自许可分发。
