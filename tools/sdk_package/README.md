# RGBD 深度相机 C/C++ SDK 3.0.0 · Windows 设备服务预览

获取 **8×8、16×16 或 48×32 的米制径向深度、板端 XYZ 点云、同帧 SNR／有效位和 2304×1296 RGB8 视频**。三种分辨率均有普通和高精度档。周期 66 ms、积分 600k、SNR 阈值 6 的历史实验中，两个 48×32 档的深度输出均约 15 Hz。C/C++ 共用 DLL。 [六档模式值、量程与测量条件](Doc/api/Chinese/sdk_specifications.html)

SDK 3.0.0 使用 ABI v3。普通与高精度模式的 TMF8829 默认积分均为 1200K。

[示例速查](Example/README.md) · [选择连接方式](Doc/api/Chinese/sdk_lifecycle.html) · [HTML 手册](Doc/api/Chinese/index.html) · [单文件 PDF](Doc/api/Chinese/API_REFERENCE.pdf) · [读取录制的点云](Doc/api/Chinese/sdk_data.html#sdk_recorded_ply) · [客户版点云读取说明（PDF）](Doc/api/Chinese/点云录制数据读取说明.pdf)

## 无相机先体验

在包根目录打开 PowerShell，把示例产物写到系统临时目录下的独立文件夹。

```powershell
powershell -File Script/verify-sdk.ps1 -Strict
$out = Join-Path ([IO.Path]::GetTempPath()) ('cxwl-sdk-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
./Example/bin/CxwlDepthSelfTest.exe -v -Report (Join-Path $out 'self-test.json')
Get-ChildItem -LiteralPath $out
```

`verify-sdk.ps1 -Strict` 返回 0 表示全部匹配，1 表示文件缺失、变更或多出，2 表示清单或校验过程出错。可加 `-Json` 输出机器可读结果。自检报告记录主机环境、DLL 版本、ABI 与六档能力。连接相机后运行 `./Example/bin/CxwlDepthPointCloud.exe -Out $out` 可写出 cloud.ply 和 depth.pgm。

`Example/bin` 的启动器透传参数、当前目录和退出码，程序实体与运行依赖统一位于 `SDK/bin`。双击启动的控制台结束后等待按键，从终端或脚本调用时自动退出。

## 接入相机

```powershell
./Example/bin/CxwlDepthQuickStart.exe board
./Example/bin/CxwlDepthQuickStart.exe high
./Example/bin/CxwlDepthViewer.exe
```

自行部署 Viewer 时选择 FULL。通过 Windows RNDIS 网卡接入预装设备服务的相机。Viewer 的深度、视频和点云共用一个采集会话，[窗口与鼠标操作](Example/QtViewer/README.md)。深度回调中调用 `CxwlDepth_GetFramePointCloud` 即可读取板端 XYZ，使用 `CxwlDepth_SavePointCloudPly` 可保存当前帧。帧回调中的 `stream` 区分视频／深度，使用 `row_stride_bytes` 访问图像；回调返回前复制异步处理需要的数据。

### 五分钟排查

1. **USB 枚举。** 用数据线直连相机并确认供电。在 Windows 设备管理器的「网络适配器」中观察插拔时是否出现相机的 RNDIS 网卡。若无变化，先换一根已确认可传数据的线或换 USB 口。
2. **设备服务。** 在 Viewer 中扫描并核对机身序号。服务版本不兼容时使用与出厂固件配套的 SDK。
3. **RNDIS。** 在设备管理器的「网络适配器」中确认相机的 USB 网卡已枚举。相机固定使用 `172.32.0.93/16`。初次配置网卡时，在包根目录的管理员 PowerShell 中运行 `./Script/configure_cxwl_depth_camera_usb.ps1 -Serial (Read-Host '机身上的 16 位 USB 序号')`。网卡就绪后在 Viewer 中打开「设备管理 → USB 一键连接」。 [USB 接入细节](Doc/api/Chinese/sdk_usb.html)

在包根目录运行 `powershell -File Script/diagnose_cxwl_depth_camera_usb.ps1 -OutputDirectory ([IO.Path]::GetTempPath())` 可采集主机诊断。Viewer 状态栏的「复制诊断」可复制版本、设备 ID、状态和最近一次采集错误。

### 从示例源码接入

1. C 程序调用 `CxwlDepth_Connect`，在会话所有者线程用 `CxwlDepth_WaitForEvent` 派发事件。深度与视频均收到后，调用 `CxwlDepth_Disconnect` 并处理 `BUSY`，直至释放句柄。 [C QuickStart](Example/c/QuickStart/QuickStart.c)
2. C++17 程序使用 `cxwl::Session`，在回调中复制深度帧和板端 XYZ，回调结束后使用副本。 [C++ QuickStart](Example/cpp/QuickStart/CppQuickStart.cpp)
3. 两台相机可同时插入 Windows。首次登记按机身序号核对实物，之后每个进程传入自己的 SDK 设备 ID。设备管理示例提供 `connect-usb` 和 `connect-saved ID` 两种命令。 [双机操作步骤](Example/README.md#在两台相机中选择目标)

## 约束与前置条件

| 项目 | 要求 |
|---|---|
| 系统与相机 | Windows x64，配套 CXWL SC3336 + TMF8829 相机。 [版本兼容矩阵与验收范围](Doc/api/Chinese/sdk_specifications.html#sdk_validation_scope) |
| 会话与线程 | 单进程单采集会话；在创建句柄的线程调用；帧地址仅在回调期间有效。 |
| 板端 | CXWL 相机预装 `1.0.0-preview.2-usb-direct` 设备服务并配备本机 F6 标定。旧固件使用原配 SDK。 |
| USB 与权限 | 数据线、稳定供电、相机 RNDIS 网卡；首次驱动安装与 USB 网络配置可能需要管理员权限。已配置设备日常采集使用普通用户。 |
| 空间 | 建议预留 300 MiB，用于解压及更新临时副本；这是余量建议，不是包体积。实际文件与各档字节数见 [package-size.json](Doc/package-size.json)，构建／采集数据另计。 |

完整运行条件及工具链见[构建与部署](Doc/api/Chinese/sdk_build.html)。

## 构建与部署

编译 C/C++ 示例需 VS 2022 v143 x64、Windows SDK 10.0.22621.0 与 CMake 3.21+；运行预编译程序不需要这些开发工具。客户与发布构建共用入口：

```powershell
powershell -File Script/build-examples.ps1 -BuildDirectory ../cxwl-build -RunOfflineTests
```

默认只构建七个 C/C++ 示例。`CxwlDepthSelfTest` 部署 MINIMAL 至 `bin/MINIMAL/Release`；采集示例部署 STANDARD 至 `bin/STANDARD/Release`。可选 Viewer 需 Qt 6.11.1 MSVC x64，使用 `-WithViewer -QtRoot <Qt目录>`，输出至 `bin/FULL/Release`。

| 部署档位 | 内容与用途 |
|---|---|
| MINIMAL | SDK、Qt Core/Gui/Network、MSVC 基础运行库及许可；用于查询与保存 XYZ 副本。 |
| STANDARD | 默认采集部署，包含 MINIMAL、媒体解码及 USB 直连通信。 |
| FULL | STANDARD + Qt Widgets/Svg、窗口插件与资源，供 Viewer 使用。 |

`cxwl_deploy_runtime(app PROFILE MINIMAL)` 可用于查询工具，采集程序选择 STANDARD。运行依赖包含 Qt，C/C++ 调用方无需 Qt 头文件或链接库。运行 `python Script/generate_runtime_components.py --check` 可复核 JSON 和 CMake 的三档清单。 [安装、重定位、vcpkg／Conan](Integration/README.md)

## 升级、验证与支持

ABI v1 和 v2 应用须用 ABI v3 的头文件与库重新编译。[迁移说明](Doc/api/Chinese/API_REFERENCE.md#abi-v3-迁移)

`sdk-manifest.json` 记录文件 SHA256，`verify-sdk.ps1 -Strict` 同时检查额外文件。 [本包构建记录](Doc/build-validation.json) · [示例构建命令](Doc/example-build-validation.json)

主机画像记录本次打包的一次离线自检，含进程启动至采样点的耗时、CPU 时间和内存。 [离线主机画像](Doc/host-profile.json) · [性能口径与网络负载](Doc/api/Chinese/sdk_specifications.html)

遇到问题先保存错误码和状态，再运行 `Script/diagnose_cxwl_depth_camera_usb.ps1`；[排障与 dump 收集](Doc/api/Chinese/sdk_errors.html)。

## 许可

客户可免费在商业 C/C++ 应用中动态链接 SDK 并分发必要运行依赖。`CxwlDepthSdk.lib` 是 DLL 导入库。 [产品许可](LICENSE) · [示例 MIT 许可](Example/LICENSE) · [第三方许可](Licenses/Third-Party-Notices.txt)
