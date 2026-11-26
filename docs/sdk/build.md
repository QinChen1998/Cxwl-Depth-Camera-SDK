@page sdk_build 构建示例与运行部署

从无需硬件的自检开始，再按应用是否采集和显示来选择运行依赖。
## 系统要求

运行预编译 Viewer 和示例需要 Windows x64。首次安装驱动和配置 USB 网络可能需要管理员权限，已配置的设备可用普通用户运行。构建环境和复现命令记录在包内 `Doc/build-validation.json`。相机和 Windows 版本的适用范围见 @ref sdk_validation_scope "版本兼容矩阵与验收范围"。

使用直连 USB 数据线与稳定供电。相机 RNDIS 网卡需要 IPv4 地址，并允许访问 TCP 8943。应用接收 RGB8 解码结果与米制深度。部署文件字节数见 Doc/package-size.json。

## 包目录

| 目录 | 用途 |
|---|---|
| SDK/include | DepthSdk.h、DepthSdkVersion.h；可选 C++ 辅助头 DepthSdk.hpp |
| SDK/lib | CxwlDepthSdk.lib，DLL 导入库 |
| SDK/bin | SDK、Qt／FFmpeg／MSVC 运行依赖与 USB 安全通信插件 |
| SDK/cmake | 版本化 find_package 和导入目标 CXWL::DepthSdk |
| Example/c、Example/cpp、Example/common | C/C++ 示例及共享诊断函数 |
| Example/QtViewer | MIT 许可的完整 Qt Widgets 示例源码 |
| Example/bin | 可直接双击的启动器，共用 SDK/bin |
| Doc/api/Chinese | HTML、唯一完整 Markdown API、JSON schemas |
| Script | 示例构建、USB 诊断、严格校验和部署清单生成 |
| Licenses | 第三方许可、构建配置和来源材料 |

## C 与 C++

以下命令在包根目录执行，默认无需 Qt 开发环境：

```powershell
cmake -S Example -B ../cxwl-build -G "Visual Studio 17 2022" -A x64
cmake --build ../cxwl-build --config Release
../cxwl-build/bin/MINIMAL/Release/CxwlDepthSelfTest.exe
../cxwl-build/bin/STANDARD/Release/CxwlDepthQuickStart.exe
```

MSVC 项目必须使用 UTF-8 源码选项；CMake 导入目标自动传递 /utf-8。C 使用 DepthSdk.h；C++ 可直接用同一接口或 DepthSdk.hpp，不需要另一套 DLL。CMake 3.21+ 的部署脚本逐文件 copy_if_different，避免反复重写未变运行库。

七个示例也可通过 `powershell -File Script/build-examples.ps1 -BuildDirectory ../cxwl-build -RunOfflineTests` 构建并自检。自检输出目录为 `bin/MINIMAL/Release`，采集示例为 `bin/STANDARD/Release`。构建目录放在包外可保持严格清单检查通过。

## QtViewer

额外需要 Qt 6.11.1 MSVC x64 的 Widgets、Network（及其 Core/Gui 依赖）。点云通过 Qt Widgets 软件光栅绘制，保留深度遮挡、颜色插值、点状／网格、坐标轴与相机交互；无需 Quick 3D、QML 或 OpenGL。

```powershell
$qtRoot = Read-Host '请输入 Qt 6.11.1 MSVC x64 的安装目录'
cmake -S Example -B ../cxwl-viewer-build -G "Visual Studio 17 2022" -A x64 -DCXWL_BUILD_QT_VIEWER=ON "-DCMAKE_PREFIX_PATH=$qtRoot"
cmake --build ../cxwl-viewer-build --config Release
```

包内 CMake 自动调用所选 Qt 的 windeployqt。完整 Viewer 只链接公开 SDK；SDK 私有实现不进入示例源码。

## 集成应用

```cmake
cmake_minimum_required(VERSION 3.21)
project(MyCameraApp LANGUAGES CXX)
find_package(CxwlDepthSdk 3.0 CONFIG REQUIRED)
add_executable(MyCameraApp main.cpp)
target_link_libraries(MyCameraApp PRIVATE CXWL::DepthSdk)
cxwl_deploy_runtime(MyCameraApp PROFILE STANDARD)
```

配置时将 CxwlDepthSdk_DIR 指向包内 SDK/cmake。仅 C 项目将 LANGUAGES 改为 C。不要只复制 SDK DLL；视频解码依赖 FFmpeg，即使客户界面不用 Qt 也需要 SDK 内部运行依赖。



## 完整性与自检

在包根目录运行 Script/verify-sdk.ps1 后运行 Example/bin/CxwlDepthSelfTest.exe。前者验证清单内全部文件的 SHA256，后者检查 DLL 可加载、公开导出、版本与六档能力，两者均不访问相机。

## 分层部署与安装

| PROFILE | 包含 | 可用范围 |
|---|---|---|
| MINIMAL | SDK、Qt Core/Gui/Network 及基础依赖、许可证 | 版本／能力查询、已有 XYZ 的 PLY 导出、离线档案操作 |
| STANDARD（默认） | MINIMAL + FFmpeg 和 USB 安全通信 | 完整 C/C++ 深度与 RGB 采集、USB 登记 |
| FULL | STANDARD + Widgets、Svg、界面插件与资源 | 需要交付 Qt Viewer 界面的应用 |

`SDK/runtime-components.json` 列出每个部署档位的文件及字节数。用 Python 3.10+ 在包根目录运行 `python Script/generate_runtime_components.py --check` 复核 JSON 和 CMake 清单，无需写入包。在用于开发的副本中运行同一命令并省略 `--check` 可重新生成两份文件。FULL 提供 Viewer 需用的运行依赖。Example/bin 的启动器将参数、工作目录和退出码传给 SDK/bin 中的实际示例。

~~~cmake
cxwl_deploy_runtime(MyCameraApp PROFILE STANDARD)
install(TARGETS MyCameraApp RUNTIME DESTINATION bin)
cxwl_install_runtime(PROFILE STANDARD DESTINATION bin)
~~~

已有 Qt 的宿主可用 `cxwl_deploy_runtime(MyCameraApp PROFILE STANDARD DESTINATION cxwl-runtime)` 将文件复制到可执行文件旁的子目录。直接导入 DLL 的应用在进入 main 前已启动加载，须在启动前配置搜索路径。需要隔离不同版本的同名 Qt DLL 时，在独立进程中运行 SDK；仅改变复制目录无法隔离同一进程中已加载的 Qt。

部署使用 CMake 3.21 的 `file(COPY_FILE ... ONLY_IF_DIFFERENT)` 在单一进程内复制清单中有变化的文件，不删除应用目录中的其他文件。缩小档位时使用新的输出目录，避免旧文件残留。离线查询与 XYZ 保存选择 MINIMAL，深度／视频采集选择 STANDARD，需要 Viewer 界面依赖时选择 FULL。交付包提供 Release 动态运行库，Debug 客户通过同一 C ABI 调用，分配型 JSON 统一用 SDK 释放。

SDK 自带安装工程和 install(EXPORT)，安装后的 find_package 与直接使用解压包保持同一目标。以下命令在包根目录执行：

~~~powershell
$prefix = [IO.Path]::GetFullPath("$PWD/../cxwl-sdk-install")
cmake -S SDK -B ../cxwl-sdk-build -G "Visual Studio 17 2022" -A x64 -DCXWL_RUNTIME_PROFILE=STANDARD
cmake --install ../cxwl-sdk-build --config Release --prefix "$prefix"
~~~

配置应用工程时，可将 `CMAKE_PREFIX_PATH` 指向安装目录 `$prefix`，也可将 `CxwlDepthSdk_DIR` 指向其下的 `share/CxwlDepthSdk`。安装目录可整体搬移，部署时可选择已安装的运行时档位或更小档位。

Integration 目录提供 vcpkg overlay 与 Conan 2 本地配方；均读取本包 SDK，校验版本，复用上述安装规则，不下载相机私有源码。操作步骤见 Integration/README.md。
