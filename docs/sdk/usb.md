@page sdk_usb USB 接入与相机登记

使用配套预装设备服务的 CXWL Depth Camera，通过可传数据的 USB 线接入上位机。相机固定使用 `172.32.0.93/16`，上位机的相机 USB 网卡使用同网段内另一个地址，例如 `172.32.0.100/16`。Windows 在设备管理器中确认 RNDIS 网卡已启用。网卡需要配置时，在 SDK 包根目录的管理员 PowerShell 中运行以下命令。

```powershell
.\Script\configure_cxwl_depth_camera_usb.ps1 -Serial (Read-Host '机身上的 16 位 USB 序号')
```

Linux 使用随包的 `sudo sh Script/cxwl-usb-network configure "$serial"`，其中 `serial` 为机身上的 16 位 USB 序号。网卡就绪后，在 Windows Viewer 的设备管理窗口选择 USB 一键连接，或在 C 程序中调用 `CxwlDepth_Connect`。

## 核对相机与登记

SDK 根据 USB 枚举序号、物理网卡和相机首次启动时生成的设备实例标识核对实物。每个上位机系统用户首次登记时自动生成独立密钥，板端登记其公钥。Windows 私钥保存在当前用户的凭据管理器，Linux 私钥保存在当前用户专用的 `usb-host-authorizations` 目录。分发镜像和 SDK 不包含这些私钥。

更换相机时先核对实物，再在设备管理中确认登记。API 客户端使用 `CxwlDepth_ScanUsb` 发起检查，等待管理器空闲，通过 `CxwlDepth_GetManagerStatus` 获取 `usb_check_id`，再将它传给 `CxwlDepth_ConnectWithOptions`。检查和连接须使用同一管理器。设备在两步之间拔插或重启时，重新扫描。

## 传输安全与迁移

把相机直接接到受控上位机的 USB 数据口。控制请求使用一次性挑战签名，板端核验主机公钥、操作权限及采集所有权。控制回执、RGB 和深度数据以明文 TCP 传输，能访问或篡改这条数据链路的一方可以观察或干预内容。设备识别依赖 USB 直连和标识核对，适用范围为受控的物理链路。工厂包签名使用独立的固件签名密钥。

USB 直连 SDK 与 `1.0.0-preview.2-usb-direct` 完整镜像配套，`1.0.0-preview.1` 和原 SDK 继续成对使用。两版的设备协议互不兼容。换用直连版或给同一台相机全擦后，在 Viewer 选择 USB 连接，或使用 SDK 的 `CXWL_DEPTH_CONNECT_PREPARE`。SDK 核对 USB 网卡和机身序号，再按新实例登记。相机名称和本地配置会沿用，重新登记完成后按列表返回的设备 ID 连接。`CXWL_DEPTH_CONNECT_SAVED_ONLY` 适用于已完成登记的实例。维护和校正权限按新主机登记标识重新授予。

## 两台相机同时接入 Windows

两台相机可同时保持 USB 连接，每台仍使用 `172.32.0.93`。按各自的 USB 序号配置对应主机网卡，主机地址分别使用同网段内未占用的地址。首次登记时，在当前 PowerShell 窗口用 `$env:CXWL_DEPTH_USB_SERIAL = Read-Host '目标相机的 16 位机身序号'` 指定实物，用 DeviceManagement 的 `scan` 核对目标，再运行 `connect-usb`。依次登记两台，记录每台双流就绪时输出的 SDK 设备 ID。登记结束后在该窗口运行 `Remove-Item Env:CXWL_DEPTH_USB_SERIAL`。

已登记设备可从 `CxwlDepth_ListSavedDevicesAlloc` 的 `id` 字段选取，每个采集进程将自己的 ID 传给 `CxwlDepthConnectOptions.device_id_utf8`，同时选择 `CXWL_DEPTH_TARGET_SAVED` 和 `CXWL_DEPTH_CONNECT_SAVED_ONLY`。单进程每次采集一台，同时采集两台需要两个进程，各自使用不同的 ID。包内设备管理示例的 `connect-saved ID` 演示了这一流程。

## 检查连接

1. 确认相机 USB 网卡已启用，并配置了可访问 `172.32.0.93` 的同网段 IPv4 地址。
2. 确认该网卡允许连接相机的 TCP 8943。在设备管理中再次扫描。
3. 出现固件不匹配提示时，核对 SDK 与相机服务的配套版本。
4. 若报告设备实例变化，核对机身序号及刷机记录，通过 USB 连接完成重新登记。

在包根目录运行 PowerShell 脚本 Script/diagnose_cxwl_depth_camera_usb.ps1 取得主机网卡、USB 接口和驱动报告。报告包含本机硬件标识。

## 维护与标定

设备登记完成后，从 `CxwlDepth_GetDeviceManagerStateAlloc` 返回的 `usb.host_authorization_id` 读取主机授权标识。每台相机与每个上位机系统用户分别登记。

停止并释放采集会话，再使用设备管理中的维护功能，或启动独立工具 CxwlRgbCalibration.exe 进行 RGB 校正。板端核对受管文件和该主机的独立操作权限。权限不足时联系设备管理员，并提供设备序号及主机授权标识。成功的 RGB 标定可从校正状态、实际 ISP 试载回执及 IQ 摘要复核。
