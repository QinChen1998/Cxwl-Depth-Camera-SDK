# Linux 深度相机 SDK 3.1.0

适用于 Ubuntu 22.04 x86-64 和小端 ARM64，公开 C ABI v3 和 C++17 封装。应用从 ABI v1 或 v2 迁移时，按 `SDK/include/DepthSdk.h` 重新编译。RV1103 相机使用随完整镜像预装的 `1.0.0-preview.3-usb-direct` 服务。本机须有 RNDIS 驱动，主机请求签名使用系统 OpenSSL 3（Ubuntu 的 `libssl3`）。

## 接入相机

使用 USB 数据线连接一台相机，确认 Linux 枚举出了它的 USB 网卡。使用机身序列号核对该接口。SDK 将 CX2 TCP 8943 连接绑定到该 USB 网卡，并核对相机回执中的序列号和设备实例标识。 [安全边界与档案迁移](sdk/usb.md)

```bash
read -r serial
sh Script/cxwl-usb-network presence "$serial"
sh Script/cxwl-usb-network check "$serial"
sudo sh Script/cxwl-usb-network configure "$serial"
```

`read` 后输入机身序列号的 16 位小写十六进制字符。相机固定使用 `172.32.0.93/16`。`configure` 为指定相机的 USB 网卡配置同网段内未占用的主机地址，已有有效地址时沿用。USB 网卡重新枚举后，可用 `check` 核对地址并按需重新配置。只有配置网卡需要管理员权限。采集应用以普通用户运行。

```bash
Example/bin/CxwlDepthDeviceManagement scan
Example/bin/CxwlDepthDeviceManagement connect-usb
Example/bin/CxwlDepthDeviceManagement list
```

Linux 设备档案与本机主机授权位于 `${XDG_DATA_HOME:-$HOME/.local/share}/DepthCameraAndTactileSensor/config`。每个 Linux 用户分别登记相机。主机授权文件保存在仅当前用户可访问的目录内，档案备份与授权文件应分开管理。连接已登记相机时，使用 `CXWL_DEPTH_TARGET_SAVED`、`CXWL_DEPTH_CONNECT_SAVED_ONLY` 和列表中的设备 ID。

普通登记主机可以使用 `CxwlDepth_Reconnect` 恢复采集。设备管理器的 `CxwlDepth_RecoverDevice` 用于清理故障锁、恢复板端运行文件，需要维护授权。

## 构建客户应用与样例

解压后保留 `SDK/lib` 内的 SONAME 符号链接、`cxwl` 和 `tls` 目录。保持 `SDK/include`、`SDK/lib/cmake` 和 `SDK/lib/pkgconfig` 的相对布局。Qt 与 FFmpeg 来自包内共享库，主机请求签名使用系统 `libcrypto.so.3`。应用只需包含 SDK 自带接口头文件。

```bash
cmake -S Example -B ../cxwl-linux-examples -DCMAKE_BUILD_TYPE=Release
cmake --build ../cxwl-linux-examples --parallel
RV_TEST_OFFLINE_ONLY=1 ../cxwl-linux-examples/CxwlDepthSelfTest
```

`CxwlDepthQuickStart`、`CxwlDepthCppQuickStart`、`CxwlDepthThreadedCapture`、`CxwlDepthQuality`、`CxwlDepthPointCloud`、`CxwlDepthDeviceManagement`、`CxwlDepthSelfTest` 均提供源码及受验二进制。实机点云示例会生成 `cloud.ply` 与 `depth.pgm`。

```bash
mkdir -p ./capture
Example/bin/CxwlDepthPointCloud -Out ./capture
Example/bin/CxwlDepthCppQuickStart
```

项目集成使用 `find_package(CxwlDepthSdk 3 CONFIG REQUIRED)` 和 `target_link_libraries(app PRIVATE CXWL::DepthSdk)`，向 CMake 传入绝对目录 `-DCMAKE_PREFIX_PATH=/path/to/SDK`。纯 C 消费者也可使用下列方法。

```bash
export PKG_CONFIG_PATH="$PWD/SDK/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
cc -std=c11 client.c $(pkg-config --cflags --libs cxwl-depth-sdk) \
  -Wl,-rpath,"$PWD/SDK/lib" -o client
```

## 采集契约

单进程内保持一个采集会话。连接、派发、状态查询及停采由创建句柄的线程处理。SDK 的回调内帧数据是借用视图，跨线程或跨回调使用前先复制。六个档位提供 RGB、径向米制深度和同帧质量图。从深度回调取得板端 XYZ，按返回的宽、高、行跨度和像素格式读取。C++ 封装提供独立帧副本。停采后等设备回执，再释放会话。

## 从源码构建 SDK

准备 Qt 6.11.1（Core、Gui、Network、Concurrent 和 OpenSSL TLS 插件）、OpenSSL 3 开发库（Ubuntu 的 `libssl-dev`）、FFmpeg 7.1.3 LGPL 运行库、CMake 3.22、Ninja、C/C++ 编译器和 patchelf。Linux 依赖目录包含 `qt-6.11.1` 和 `ffmpeg-7.1.3` 两个安装前缀。以下命令从源码根目录发起。

```bash
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$deps/qt-6.11.1"
cmake --build "$build" --parallel
python3 -B tools/linux/stage.py --arch x64 --build "$build" --deps "$deps" \
  --output "$stage" --examples-build "$work/examples"
```

ARM64 可原生构建。若使用 x86-64 主机交叉构建，设置 `CXWL_ARM64_SYSROOT` 和同版本的 `CXWL_QT_HOST_PATH`，使用 `tools/linux/aarch64-toolchain.cmake`，并给首次配置和 `stage.py --toolchain` 传入该工具链文件。`--deps` 指向 ARM64 的运行库安装前缀。对两种架构各自运行离线和实机测试，再生成各自的本地发布包。

## 校验交付

同一目录内放置 `.tar.gz` 与 `.sha256` 文件。运行 `sha256sum -c <归档名>.sha256`，解压后在包根目录运行 `sha256sum --strict -c SHA256SUMS`。库与示例的验收写在 `Doc/build-validation.json`，RV1103 验收写在 `Doc/device-validation.json`。产品和第三方材料位于 `LICENSE` 与 `Licenses`。
