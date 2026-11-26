# SDK 示例

在 SDK 包根目录打开 PowerShell。先做完整性检查，再运行离线自检。以下命令将产物保存到系统临时目录。

```powershell
powershell -File Script/verify-sdk.ps1 -Strict
$out = Join-Path ([IO.Path]::GetTempPath()) ('cxwl-sdk-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
./Example/bin/CxwlDepthSelfTest.exe -v -Report (Join-Path $out 'self-test.json')
```

接上预装设备服务 1.0.0-preview.1 的相机后，先运行 QuickStart 确认深度和视频均到达，再运行 `./Example/bin/CxwlDepthPointCloud.exe -Out $out` 保存同帧 XYZ 和深度图。C 使用 `CxwlDepth_Connect`，C++17 使用 `cxwl::Session`，两者加载相同的 Windows DLL。维护、修复或恢复前先释放目标相机的采集会话。

| 程序 | 参数 | 预期输出 |
|---|---|---|
| `CxwlDepthSelfTest.exe` | `[-v] [-Report <file.json>]` | 一行通过摘要。加 `-v` 打印每项检查，加 `-Report` 写入环境、DLL／ABI、导出和各项结果。无需相机。 [C 源码](c/SelfTest/SelfTest.c) |
| `CxwlDepthPointCloud.exe` | `[-Out <已有目录>]` | `cloud.ply` 保存有效 XYZ 点（米），`depth.pgm` 保存径向深度（大端 16 位毫米，0 表示缺测）。省略 `-Out` 使用当前目录。需要已登记相机。 [C 源码](c/PointCloud/PointCloud.c) |
| `CxwlDepthQuickStart.exe` | `[board｜high] [--reconnect]` | 2304×1296 RGB8、48×32 米制径向深度和采集代次。默认普通模式。指定 `--reconnect` 后再做一次重连并等待两条流。 [C 源码](c/QuickStart/QuickStart.c) |
| `CxwlDepthQuality.exe` | 无 | 能力、深度帧和同帧 SNR／有效位统计。 [C 源码](c/DepthQuality/DepthQuality.c) |
| `CxwlDepthDeviceManagement.exe` | `list`、`scan`、`connect-usb`、`connect-saved ID`、`select ID`、`remove ID`、`repair ID`、`maintain ID`、`recover ID` | 默认列出登记设备。`connect-usb` 核验并登记 USB 相机。`connect-saved ID` 采集指定设备，两路就绪后打印 ID 并断开。`select ID` 设置后续默认设备。 [C 源码](c/DeviceManagement/DeviceManagement.c) |
| `CxwlDepthCppQuickStart.exe` | 无 | DLL 版本、拥有数据的深度副本尺寸和同帧板端点数。 [C++ 源码](cpp/QuickStart/CppQuickStart.cpp) |
| `CxwlDepthThreadedCapture.exe` | 无 | 工作线程拥有 SDK 会话，消费线程读取并输出已复制的深度帧。 [C++ 源码](cpp/ThreadedCapture/ThreadedCapture.cpp) |
| `CxwlDepthViewer.exe` | 无 | 深度、视频和点云窗口，状态栏可复制诊断。 [Viewer 操作与构建](QtViewer/README.md) |

## 在两台相机中选择目标

两台都可以保持接线。首次登记时，在包根目录运行下面的命令，按提示输入要登记的那台机身上的 16 位 USB 序号。核对扫描结果后再执行连接。按同样步骤登记另一台。

```powershell
$manager = './Example/bin/CxwlDepthDeviceManagement.exe'
$env:CXWL_DEPTH_USB_SERIAL = Read-Host '目标相机的 16 位机身序号'
& $manager scan
# 核对目标后执行
& $manager connect-usb
Remove-Item Env:CXWL_DEPTH_USB_SERIAL
& $manager list
```

登记成功时，`connect-usb` 输出该台相机的 SDK 设备 ID。以后直接使用这个 ID，也可以从 `list` 返回的 `id` 字段取得。在两个 PowerShell 窗口各运行下列命令，分别输入不同设备 ID，即可各自验证该台相机的深度和视频。每个进程持有一个采集会话。

```powershell
$deviceId = Read-Host '已登记的 SDK 设备 ID'
./Example/bin/CxwlDepthDeviceManagement.exe connect-saved $deviceId
```

控制台示例返回 0 表示成功，1 表示检查或运行失败，支持命令行参数的示例用 2 表示参数错误。`Example/bin` 下的启动器在文件属性中标记为 CXWL SDK Example Launcher。启动器自身返回 3 表示路径解析失败，4 表示缺少包内目标，5 表示参数读取失败，6 表示进程启动失败，7 表示未取得进程退出码。成功启动后透传子进程退出码。复现构建可运行 `powershell -File Script/build-examples.ps1 -BuildDirectory ../cxwl-build -RunOfflineTests`。 [任务与 API 对照](../Doc/api/Chinese/sdk_examples.html)
