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
