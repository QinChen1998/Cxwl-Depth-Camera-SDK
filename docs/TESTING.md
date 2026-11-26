# 本地验证

构建环境见 [项目首页](../README.md)。每次验收选用新的输出目录。正常构建后，使用下列命令验证 ABI 并构建带测试入口的桌面程序。

```powershell
python -B tests/test_depth_sdk_abi.py --dll x64/Release/CxwlDepthSdk.dll --output x64/validation/abi
./tools/build_desktop.ps1 -Configuration Release -Validation
python -B tools/run_offline_tests.py --configuration Release --qt-root $env:CXWL_QT_ROOT --local-runtime --output x64/validation/desktop
```

实机使用独立的、已登记相机档案副本。依赖 Python、NumPy 和 Pillow。将 `$profilePath` 指向档案，从当前目录运行：

```powershell
python -B tools/validate_sdk_live_outputs.py --development-runtime --runtime x64/Release --registry $profilePath --output x64/validation/camera --all-modes --transitions --seconds 2
```

实测检查视频、深度、同帧质量图、板端 XYZ、PLY 字节、六档切换和释放。两台相机使用各自的登记选择，在两个独立进程中采集。校正工具可用 `--check --serial <序列号> --device-store <档案>` 只读查询。

## Linux x86-64 与 ARM64

按 [Linux 接入说明](DEPTH_SDK_LINUX.md) 为两架构分别构建并安装一份待验运行目录。`ctest` 检查 C++ 封装与 Linux 主机授权文件的重开、权限和符号链接拒绝。使用对应架构的 Ubuntu 22.04 用户空间运行以下命令。离线入口需要 Python 3、jsonschema、C/C++ 编译器、CMake、pkg-config 和 binutils。

```bash
ctest --test-dir "$build" --output-on-failure
python3 -B tools/linux/validate_offline.py --arch x64 --stage "$stage" --output "$work/offline"
```

ARM64 将 `--arch` 改为 `arm64`。离线验收校验 ELF 架构、依赖与导出，重建随包样例，并实际运行 SDK 自检、ABI v3、JSON、C99/C11 pkg-config 客户端及离线禁用 USB 扫描检查。

在对应架构的 Linux 系统接入已授权 RV1103，按机身序列号配置 USB 网卡。实机使用隔离登记与授权目录，需要 Python 3、NumPy 和 Pillow。

```bash
bash tools/linux/validate_live.sh "$stage" "$work/live" 12
```

验收覆盖六档 RGB、径向深度、同帧质量图、板端 XYZ 和二进制 PLY，复核切换、重连、停采后重启及每次释放。另运行随包 PointCloud 和 CppQuickStart，核对 PGM、PLY 和 48×32 点位。各架构各自记录动态库与完整运行时 SHA-256。
