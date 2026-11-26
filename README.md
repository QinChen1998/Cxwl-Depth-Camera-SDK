# Cxwl Depth Camera SDK

Windows x64 深度相机 SDK 3.0.0，公开 C ABI v3，C++17 共用同一个 DLL。每个进程可采集一台相机，多个进程可按已登记身份分别选机。相机通过 USB 网卡连接 RV1103 设备服务预览版。六档深度均支持 RGB 视频、径向米制深度和板端同帧 XYZ。

## 本地构建

需要 VS 2022 v143、Windows SDK 10.0.22621.0、Qt VS Tools 和 Qt 6.11.1 MSVC x64。使用构建脚本还需要 PowerShell 7。

在项目根目录创建 `QtDevelopment.local.props`，将下面的示例路径改为本机包含 `bin\qmake.exe` 的 Qt 套件目录。Viewer、SDK 和 RGB 校正工具共用这份本地配置。

```xml
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup>
    <QtInstall>C:\Qt\6.11.1\msvc2022_64</QtInstall>
  </PropertyGroup>
</Project>
```

也可以设置用户环境变量 `CXWL_QT_ROOT` 并重启 Visual Studio。若同时配置，项目优先使用环境变量。打开或重新加载解决方案后，选择 `Release | x64` 并生成解决方案。

如需构建并部署运行依赖，从项目根目录运行：

```powershell
./tools/build_desktop.ps1 -Configuration Release
./START_RELEASE.cmd
```

[Visual Studio 解决方案](<Cxwl Depth Camera.sln>) 包含 C SDK、C 示例、桌面 Viewer 和独立 RGB 校正工具。输出在 `x64/Release`。使用 [Windows SDK 接入说明](docs/DEPTH_SDK_WINDOWS.md) 和 [C/C++ 示例](sdk/examples) 开始开发。公开入口为 [DepthSdk.h](sdk/DepthSdk.h)，点云在深度回调内通过 `CxwlDepth_GetFramePointCloud` 取得。

- [项目结构与构建材料](docs/ARCHITECTURE.md)
- [离线及实机验证](docs/TESTING.md)
- [点云录制与读取](docs/POINT_CLOUD_RECORDING.md)
- [独立 RGB 校正工具](docs/sdk/rgb_lsc.md)
- [第三方许可](docs/OPEN_SOURCE_LICENSES.md)
- [提交、PR 与发布规则](CONTRIBUTING.md)
