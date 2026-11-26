@page sdk_usb USB 接入与相机登记

使用配套预装设备服务的 CXWL Depth Camera，通过可传数据的 USB 线接入 Windows x64。打开设备管理器，在网络适配器中确认相机的 RNDIS 网卡已启用。使用稳定供电。在 Viewer 的设备管理窗口选择 USB 一键连接，或在 C 程序中调用 CxwlDepth_Connect。

## 核对相机与登记

SDK 根据 USB 枚举序号、网卡和相机证书识别实物。登记成功后，主机身份由当前 Windows 用户的凭据管理器保存。更换相机时，先核对实物，再在设备管理中确认登记。API 客户端使用 `CxwlDepth_ScanUsb` 发起检查，等待管理器空闲，通过 `CxwlDepth_GetManagerStatus` 获取 `usb_check_id`，再将它传给 `CxwlDepth_ConnectWithOptions`。检查和连接须使用同一管理器。设备在两步之间拔插或重启时，重新扫描。

## 两台相机同时接入 Windows

两台相机可以保持连接，即使两台 USB 网卡上的相机地址相同。首次登记时，在当前 PowerShell 窗口用 `$env:CXWL_DEPTH_USB_SERIAL = Read-Host '目标相机的 16 位机身序号'` 指定实物，用 DeviceManagement 的 `scan` 核对目标，再运行 `connect-usb`。依次登记两台，记录每台双流就绪时输出的 SDK 设备 ID。登记结束后在该窗口运行 `Remove-Item Env:CXWL_DEPTH_USB_SERIAL`。

已登记设备可从 `CxwlDepth_ListSavedDevicesAlloc` 的 `id` 字段选取，每个采集进程将自己的 ID 传给 `CxwlDepthConnectOptions.device_id_utf8`，同时选择 `CXWL_DEPTH_TARGET_SAVED` 和 `CXWL_DEPTH_CONNECT_SAVED_ONLY`。单进程每次采集一台，同时采集两台需要两个进程，各自使用不同的 ID。包内设备管理示例的 `connect-saved ID` 演示了这一流程。

## 检查连接

1. 确认 USB 网卡启用并取得 IPv4 地址。
2. 确认该网卡允许连接相机的 TCP 8943。在设备管理中再次扫描。
3. 出现固件不匹配提示时，向设备供应方索取与设备服务 1.0.0-preview.1 配套的固件。旧固件继续使用原配稳定版 SDK。
4. 若报告设备身份变化，核对机身序号及替换记录，再在设备管理中确认。

在包根目录运行 PowerShell 脚本 Script/diagnose_cxwl_depth_camera_usb.ps1 取得主机网卡、USB 接口和驱动报告。报告包含本机硬件标识。

## 维护与标定

设备登记完成后，从 `CxwlDepth_GetDeviceManagerStateAlloc` 返回的 `usb.host_authorization_id` 读取主机授权标识。每台相机和每个 Windows 用户分别登记。

停止并释放采集会话，再使用设备管理中的维护功能，或启动独立工具 CxwlRgbCalibration.exe 进行 RGB 校正。板端核对受管文件和该主机的独立操作权限。权限不足时联系设备管理员，并提供设备序号及主机授权标识。成功的 RGB 标定可从校正状态、实际 ISP 试载回执及 IQ 摘要复核。
