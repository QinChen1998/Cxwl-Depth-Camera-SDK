# 安装、重定位与包管理

本版本为 3.0.0 / ABI v3。ABI v1、v2 应用须使用本包头文件及导入库重新编译，再部署配套 DLL 和运行依赖。 [迁移说明](../Doc/api/Chinese/API_REFERENCE.md#abi-v3-迁移)

## 三种部署档位

| PROFILE | 用途 |
|---|---|
| MINIMAL | 版本／能力查询、离线投影、JSON 和已登记档案查询。 |
| STANDARD | 默认部署档位。完整 C/C++ 采集，包含媒体解码与设备安全通信。 |
| FULL | STANDARD 加 Qt Widgets／SVG、平台插件和 Viewer 显示资源。 |

`SDK/bin` 集中存放程序和运行依赖。`Example/bin` 的启动器透传参数、工作目录和退出码。用 `python Script/generate_runtime_components.py --check` 只读复核 JSON 和 CMake 的部署清单。重建的示例由 `cxwl_deploy_runtime` 按所选档位部署所需文件。

## 安装到独立前缀

在包根目录执行：

~~~powershell
$prefix = [IO.Path]::GetFullPath("$PWD/../cxwl-sdk-install")
cmake -S SDK -B ../cxwl-sdk-build -G "Visual Studio 17 2022" -A x64 -DCXWL_RUNTIME_PROFILE=STANDARD
cmake --install ../cxwl-sdk-build --config Release --prefix "$prefix"
~~~

安装内容包括 include、lib、bin 和 share/CxwlDepthSdk。开发文件为 Development 组件，运行文件为 Runtime 组件；通常完整安装。可以整体复制／移动安装前缀，再将消费者的 CMAKE_PREFIX_PATH 指向新位置。导入目标和部署清单不记录原解压路径。

~~~cmake
find_package(CxwlDepthSdk 3.0 CONFIG REQUIRED)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE CXWL::DepthSdk)
cxwl_deploy_runtime(app PROFILE STANDARD)
install(TARGETS app RUNTIME DESTINATION bin)
cxwl_install_runtime(DESTINATION bin PROFILE STANDARD)
~~~

离线查询与投影选择 MINIMAL，深度／视频采集选择 STANDARD，需要 Viewer 界面依赖时选择 FULL。降档时使用新的输出目录。默认部署会更新目标目录中的同名 SDK 依赖。要放入单独的子目录，可用 `cxwl_deploy_runtime(app PROFILE STANDARD DESTINATION cxwl-runtime)`。直接链接的应用需在进程启动前配置 DLL 搜索路径。同一进程中已有不同版本的 Qt 时，可由独立进程承载采集。

## vcpkg 本地 overlay

完整交付包中的 Integration/vcpkg/ports 可作为 overlay；不向公共 registry 上传二进制。

~~~powershell
vcpkg install cxwl-depth-sdk:x64-windows --overlay-ports="<SDK包>/Integration/vcpkg/ports"
vcpkg install "cxwl-depth-sdk[core]:x64-windows" --overlay-ports="<SDK包>/Integration/vcpkg/ports"
vcpkg install "cxwl-depth-sdk[usb-tools]:x64-windows" --overlay-ports="<SDK包>/Integration/vcpkg/ports"
vcpkg install "cxwl-depth-sdk[viewer,usb-tools]:x64-windows" --overlay-ports="<SDK包>/Integration/vcpkg/ports"
~~~

依次为 STANDARD（使用已登记网络设备）、MINIMAL、STANDARD 加 USB 工具、FULL 加 USB 工具。首次 USB 发现／登记需要 `usb-tools`。目标 triplet 必须为 Windows x64 动态库。消费者使用 vcpkg 工具链后调用 `find_package(CxwlDepthSdk 3.0 CONFIG REQUIRED)`。vcpkg 集成将随包的 Release DLL 用于 Debug 和 Release 客户应用，SDK 分配的 JSON 统一由 SDK 释放。

将 overlay 单独复制到项目时，把 `CXWL_SDK_ROOT` 指向包含 `sdk-manifest.json` 的完整解压目录。复制当前 Windows x64 动态 triplet 到自定义 triplet 目录，加入以下设置，使 vcpkg 将路径带入构建环境并计入 ABI 缓存键。 [vcpkg triplet 变量](https://learn.microsoft.com/en-us/vcpkg/users/triplets#vcpkg_env_passthrough)

~~~cmake
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
list(APPEND VCPKG_ENV_PASSTHROUGH CXWL_SDK_ROOT)
~~~

例如将上述 triplet 保存为 `./triplets/x64-windows-cxwl.cmake`，将复制的 port 放在 `./ports/cxwl-depth-sdk`。

~~~powershell
$env:CXWL_SDK_ROOT = (Resolve-Path -LiteralPath (Read-Host '请输入 SDK 解压目录')).Path
vcpkg install --triplet x64-windows-cxwl --overlay-triplets=./triplets --overlay-ports=./ports
~~~

上述命令在使用下方依赖声明的应用目录运行。port 会检查 SDK 包与 overlay 版本一致。

对于 Visual Studio 附带的仅支持 manifest 模式的 vcpkg，在应用目录的 vcpkg.json 中添加依赖：

~~~json
{"name":"my-camera-app","version-string":"1","dependencies":["cxwl-depth-sdk"]}
~~~

随后在应用目录执行 vcpkg install --triplet x64-windows --overlay-ports="<SDK包>/Integration/vcpkg/ports"。MINIMAL 用 {"name":"cxwl-depth-sdk","default-features":false}；FULL 再添加 "features":["viewer"]。

## Conan 2 本地配方

在完整交付包中执行；SDK 对应的预编译材料会进入本地 Conan cache，无需 SDK 私有源码。

~~~powershell
conan profile detect
conan create Integration/conan -s build_type=Release -o "&:runtime_profile=STANDARD"
~~~

将 `cxwl-depth-sdk/3.0.0` 加入应用的 requires，使用 CMakeToolchain 和 CMakeDeps。Conan 配方固定为 Windows x64、MSVC 与动态 Release CRT（/MD）。Debug 或其他 CRT 配置会得到明确错误。可将 `runtime_profile` 改为 MINIMAL 或 FULL，并使用已安装的 `cxwl_deploy_runtime`／`cxwl_install_runtime` 随应用部署运行依赖和许可材料。

安装目标规则参照 [CMake install](https://cmake.org/cmake/help/v3.21/command/install.html)；本地 overlay 参照 [vcpkg 文档](https://learn.microsoft.com/en-us/vcpkg/concepts/overlay-ports)，预置 CMake 配置的 Conan 接入参照 [CMakeDeps 文档](https://docs.conan.io/2/reference/tools/cmake/cmakedeps.html)。
