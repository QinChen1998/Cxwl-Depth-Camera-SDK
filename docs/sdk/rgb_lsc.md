@page sdk_rgb_lsc Windows 独立 RGB 校正工具

在解决方案中构建 CxwlRgbCalibration。运行 CxwlRgbCalibration.exe，按 USB 序列号选择已登记相机，点击“查看校正状态”。工具使用 SDK 的 USB 网卡核对和本机主机授权。使用单独设备档案时点击“选择设备档案”，也可以传入 --device-store 路径。

需要重新标定时，先停止所选相机的采集，让均匀照明的平整白板充满画面。点击“采集并写入校正”，核对序列号后确认。工具采集 21 帧，计算双绿网格、试加载并核验板端写入结果。结果会区分已写入、现有校正有效、样本不足和设备错误。日常采集由板端 ISP 使用已经部署的校正。

只读查询可使用以下命令。

```powershell
./CxwlRgbCalibration.exe --check --serial <USB序列号>
```
