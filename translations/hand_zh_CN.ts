<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_CN" sourcelanguage="en_US">
<context>
    <name>DepthMapWidget</name>
    <message>
        <source>Fixed color range</source>
        <translation>固定色标</translation>
    </message>
    <message>
        <source>Dynamic + histogram equalization</source>
        <translation>动态＋直方图均衡</translation>
    </message>
    <message>
        <source>Depth map color mode</source>
        <translation>深度图着色模式</translation>
    </message>
    <message>
        <source>Equalize depths from 0.10 to 10.00 m in each frame. Colors can vary across frames.</source>
        <translation>逐帧均衡 0.10–10.00 m 的有效深度。同一深度的颜色可能随帧变化。</translation>
    </message>
    <message>
        <source>Checking device and board files...</source>
        <translation>正在检查设备连接和板端文件…</translation>
    </message>
    <message>
        <source>Starting camera and depth capture...</source>
        <translation>正在启动摄像头和深度采集…</translation>
    </message>
    <message>
        <source>Waiting for depth and video frames...</source>
        <translation>正在等待深度图和原视频帧…</translation>
    </message>
    <message>
        <source>Select standard or high accuracy for 48×32 depth.</source>
        <translation>为 48×32 深度图选择普通或高精度档。</translation>
    </message>
    <message>
        <source>Input angle (%1°)</source>
        <translation>输入角度（%1°）</translation>
    </message>
    <message>
        <source>Native ToF angle</source>
        <translation>原生 ToF 角度</translation>
    </message>
    <message>
        <source>Clockwise angle (0–359°):</source>
        <translation>顺时针角度（0–359°）：</translation>
    </message>
    <message>
        <source>Rotate the displayed ToF grid clockwise. Raw measurements and saved images keep their orientation.</source>
        <translation>顺时针旋转 ToF 显示网格。原始测量和保存图像的方向保持原样。</translation>
    </message>
    <message>
        <source>%1×%2 standard</source>
        <translation>%1×%2 普通</translation>
    </message>
    <message>
        <source>%1×%2 high accuracy</source>
        <translation>%1×%2 高精度</translation>
    </message>
    <message>
        <source>Switching imaging scheme...</source>
        <translation>正在切换成像方案…</translation>
    </message>
    <message>
        <source>Waiting for the new depth image...</source>
        <translation>正在等待新深度画面…</translation>
    </message>
    <message>
        <source>Switch complete</source>
        <translation>切换完成</translation>
    </message>
    <message>
        <source>Imaging scheme switch progress</source>
        <translation>成像方案切换进度</translation>
    </message>
    <message>
        <source>Estimated progress. Completion waits for a valid new depth image.</source>
        <translation>预估进度；收到有效的新深度图后完成。</translation>
    </message>
    <message>
        <source>No fresh dToF data</source>
        <translation>等待新的 dToF 数据</translation>
    </message>
    <message>
        <source>RV session changed</source>
        <translation>RV会话已更换</translation>
    </message>
    <message>
        <source>RV session unavailable</source>
        <translation>RV会话不可用</translation>
    </message>
    <message>
        <source>Save depth map</source>
        <translation>保存深度图</translation>
    </message>
    <message>
        <source>Select capture folder</source>
        <translation>选择保存目录</translation>
    </message>
    <message>
        <source>Save failed: %1</source>
        <translation>保存失败：%1</translation>
    </message>
    <message>
        <source>Saved to %1</source>
        <translation>已保存到 %1</translation>
    </message>
    <message>
        <source>No current depth frame. Start capture and wait for a frame.</source>
        <translation>暂无深度帧。请开始采集并等待首帧。</translation>
    </message>
    <message>
        <source>Invalid depth packet</source>
        <translation>深度数据包无效</translation>
    </message>
    <message>
        <source>Imaging scheme:</source>
        <translation>成像方案：</translation>
    </message>
    <message>
        <source>Apply</source>
        <translation>应用</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <source>Start</source>
        <translation>开始</translation>
    </message>
    <message>
        <source>Stop</source>
        <translation>停止</translation>
    </message>
    <message>
        <source>Reconnect now</source>
        <translation>立即重连</translation>
    </message>
    <message>
        <source>Start, Stop and Reconnect apply to the shared camera + depth stream.</source>
        <translation>开始、停止和立即重连同时作用于原视频与深度图。</translation>
    </message>
    <message>
        <source>Black outside range</source>
        <translation>超范围黑色</translation>
    </message>
    <message>
        <source>Minimum (m)</source>
        <translation>最小值（m）</translation>
    </message>
    <message>
        <source>Maximum (m)</source>
        <translation>最大值（m）</translation>
    </message>
</context>
<context>
    <name>DepthSdkClient</name>
    <message>
        <source>Camera is not started</source>
        <translation>相机未启动</translation>
    </message>
    <message>
        <source>Starting capture...</source>
        <translation>正在启动采集…</translation>
    </message>
    <message>
        <source>Stopping capture...</source>
        <translation>正在停止…</translation>
    </message>
    <message>
        <source>Capture stopped</source>
        <translation>已停止采集</translation>
    </message>
    <message>
        <source>Could not start or resume capture. Check the device and try again.</source>
        <translation>启动或恢复采集失败，请检查设备后重试。</translation>
    </message>
    <message>
        <source>Could not confirm that capture stopped. Try again.</source>
        <translation>无法确认采集已停止，请重试。</translation>
    </message>
</context>
<context>
    <name>ImagePresentation</name>
    <message>
        <source>0° (Original)</source>
        <translation>0°（原方向）</translation>
    </message>
    <message>
        <source>90° clockwise</source>
        <translation>顺时针 90°</translation>
    </message>
    <message>
        <source>180°</source>
        <translation>180°</translation>
    </message>
    <message>
        <source>270° clockwise</source>
        <translation>顺时针 270°</translation>
    </message>
    <message>
        <source>Display rotation</source>
        <translation>画面旋转</translation>
    </message>
    <message>
        <source>Rotate both depth and camera displays on this computer. Source data and saved images keep their original orientation.</source>
        <translation>在上位机同步旋转深度图与摄像头画面。源数据和保存的图片保持原方向。</translation>
    </message>
</context>
<context>
    <name>RtspCameraPage</name>
    <message>
        <source>Start or Reconnect resets a latched sensor fault after preserving diagnostics and verifying capture ownership, then waits for fresh camera + depth data.</source>
        <translation>点击“开始”或“立即重连”将保存故障日志、核对采集所有权后复位传感器，并等待新的相机和深度数据。</translation>
    </message>
    <message>
        <source>Start, Stop and Reconnect apply to the shared camera + depth stream.</source>
        <translation>开始、停止和重连作用于相机与深度的共享数据流。</translation>
    </message>
    <message>
        <source>Start</source>
        <translation>开始</translation>
    </message>
    <message>
        <source>Stop</source>
        <translation>停止</translation>
    </message>
    <message>
        <source>Reconnect now</source>
        <translation>立即重连</translation>
    </message>
    <message>
        <source>Resolution: %1 | FPS: %2</source>
        <translation>分辨率：%1 | FPS：%2</translation>
    </message>
    <message>
        <source>Display rotation</source>
        <translation>画面旋转</translation>
    </message>
    <message>
        <source>Rotate the camera video.</source>
        <translation>旋转摄像头原视频。</translation>
    </message>
</context>
<context>
    <name>RvDepthView</name>
    <message>
        <source>(%1, %2) dToF radial: %3 m</source>
        <translation>（%1，%2）径向距离 %3 米</translation>
    </message>
    <message>
        <source>Waiting for CXWL Depth Camera depth</source>
        <translation>等待 CXWL Depth Camera 的深度数据</translation>
    </message>
    <message>
        <source>Invalid depth</source>
        <translation>无效深度</translation>
    </message>
    <message>
        <source>Last displayed frame; depth reading expired</source>
        <translation>保留的上一帧画面；深度读数已过期</translation>
    </message>
</context>
<context>
    <name>RvDeviceManagerDialog</name>
    <message><source>Recover device</source><translation>恢复设备</translation></message>
    <message><source>Device recovery is unavailable in offline mode.</source><translation>离线模式下无法恢复设备。</translation></message>
    <message><source>Could not start device recovery. Stop capture and check the device connection.</source><translation>无法开始恢复设备。请停止采集并检查设备连接。</translation></message>
    <message><source>Recovering device...</source><translation>正在恢复设备…</translation></message>
    <message><source>Device recovery failed. Check the diagnostic report.</source><translation>设备恢复失败，请查看诊断报告。</translation></message>
    <message><source>Device recovered. Click Start to begin capture.</source><translation>设备已恢复，点击开始即可采集。</translation></message>
    <message>
        <source>Device Manager</source>
        <translation>设备管理</translation>
    </message>
    <message>
        <source>Saved devices</source>
        <translation>已登记设备</translation>
    </message>
    <message>
        <source>Connect via USB</source>
        <translation>USB 一键连接</translation>
    </message>
    <message>
        <source>Use selected device</source>
        <translation>使用所选设备</translation>
    </message>
    <message>
        <source>Remove device</source>
        <translation>移除设备</translation>
    </message>
    <message>
        <source>Check board files</source>
        <translation>检查板端文件</translation>
    </message>
    <message>
        <source>Check correction grid</source>
        <translation>检查矫正网格</translation>
    </message>
    <message>
        <source>Keep the depth camera still and the lighting steady. The device will collect 21 raw frames at six exposure levels to calculate the correction grid in memory. The frames will not be saved. The grid will be deployed after the ISP trial.</source>
        <translation>请保持深度相机不动，光照稳定。设备将按六档曝光采集 21 帧原始画面，在内存中计算矫正网格，画面不会保存。通过 ISP 试加载后，网格会部署到此设备。</translation>
    </message>
    <message>
        <source>Choose Skip to continue connecting with the current camera settings.</source>
        <translation>选择“跳过”将使用当前相机设置继续连接。</translation>
    </message>
    <message>
        <source>Start calibration</source>
        <translation>开始标定</translation>
    </message>
    <message>
        <source>Skip</source>
        <translation>跳过</translation>
    </message>
    <message>
        <source>Correction grid calibration is unavailable in offline mode.</source>
        <translation>离线模式下无法标定矫正网格。</translation>
    </message>
    <message>
        <source>Could not check the correction grid. Check the device connection.</source>
        <translation>无法检查矫正网格，请确认设备连接。</translation>
    </message>
    <message>
        <source>Checking the correction grid...</source>
        <translation>正在检查矫正网格…</translation>
    </message>
    <message>
        <source>Capturing, calculating and applying the correction grid...</source>
        <translation>正在采集、计算并替换矫正网格…</translation>
    </message>
    <message>
        <source>Correction grid progress</source>
        <translation>矫正网格处理进度</translation>
    </message>
    <message>
        <source>This device does not have a compatible SC3336 correction grid.</source>
        <translation>此设备不支持当前 SC3336 矫正网格方案。</translation>
    </message>
    <message>
        <source>Could not start correction grid calibration. Stop capture and try again.</source>
        <translation>无法开始标定，请停止采集后重试。</translation>
    </message>
    <message>
        <source>Correction grid calibration cancelled.</source>
        <translation>已取消矫正网格标定。</translation>
    </message>
    <message>
        <source>The board IQ was replaced and verified.</source>
        <translation>板端 IQ 替换成功，已通过读回校验。</translation>
    </message>
    <message>
        <source>The board already uses this correction grid. The IQ was verified.</source>
        <translation>板端已使用此矫正网格，IQ 已通过读回校验。</translation>
    </message>
    <message>
        <source>Correction grid replacement failed. The RAW exposure differences were insufficient. Improve the lighting and try again.</source>
        <translation>矫正网格替换失败。原始画面的曝光差不足，请改善照明后重试。</translation>
    </message>
    <message>
        <source>Could not confirm the IQ replacement. Reconnect the device and check the grid status.</source>
        <translation>未能确认板端 IQ 的替换结果。请重新连接设备后检查矫正网格。</translation>
    </message>
    <message>
        <source>Correction grid replacement failed. Check the device connection and read the grid status.</source>
        <translation>矫正网格替换失败。请确认设备连接并查询网格状态。</translation>
    </message>
    <message>
        <source>Correction grid replacement failed. %1</source>
        <translation>矫正网格替换失败。%1</translation>
    </message>
    <message>
        <source>Repair the board runtime and archive older rollback copies on this computer.</source>
        <translation>修复板端运行文件，并将较旧的回滚副本归档到本机。</translation>
    </message>
    <message>
        <source>CXWL Depth Camera %1</source>
        <translation>CXWL Depth Camera %1</translation>
    </message>
    <message>
        <source>%1 (current)</source>
        <translation>%1（当前）</translation>
    </message>
    <message>
        <source>Could not read the saved device list.</source>
        <translation>无法读取已登记设备列表。</translation>
    </message>
    <message>
        <source>Stop capture to switch or maintain saved devices.</source>
        <translation>停止采集后可以切换设备或检查板端文件。</translation>
    </message>
    <message>
        <source>Connect a CXWL Depth Camera with a USB data cable to get started.</source>
        <translation>使用 USB 数据线连接 CXWL Depth Camera。</translation>
    </message>
    <message>
        <source>USB connection is unavailable in offline mode.</source>
        <translation>离线模式下无法通过 USB 连接设备。</translation>
    </message>
    <message>
        <source>Connecting to CXWL Depth Camera via USB...</source>
        <translation>正在通过 USB 连接 CXWL Depth Camera…</translation>
    </message>
    <message>
        <source>The CXWL Depth Camera was verified and selected for imaging.</source>
        <translation>已验证 CXWL Depth Camera，并设为当前采集设备。</translation>
    </message>
    <message>
        <source>USB connection setup ended.</source>
        <translation>USB 连接流程已结束。</translation>
    </message>
    <message>
        <source>Wait for the current device operation to finish, then try again.</source>
        <translation>请等待当前设备操作完成后再试。</translation>
    </message>
    <message>
        <source>Device selected. Click Start in either imaging panel when ready.</source>
        <translation>已选中设备，可在深度图或原视频点击“开始”。</translation>
    </message>
    <message>
        <source>Could not select the device. Reopen Device Manager and try again.</source>
        <translation>无法切换设备，请重新打开设备管理后重试。</translation>
    </message>
    <message>
        <source>Remove the selected device from this computer?</source>
        <translation>从本机移除所选设备？</translation>
    </message>
    <message>
        <source>Device removed.</source>
        <translation>已移除设备。</translation>
    </message>
    <message>
        <source>Could not remove the device. Reopen Device Manager and try again.</source>
        <translation>无法移除设备，请重新打开设备管理后重试。</translation>
    </message>
    <message>
        <source>Board file maintenance is unavailable in offline mode.</source>
        <translation>离线模式下无法维护板端文件。</translation>
    </message>
    <message>
        <source>Check the selected device, repair required files, and archive older runtime files on this computer?</source>
        <translation>检查所选设备、修复所需文件，并将旧运行文件归档到本机？</translation>
    </message>
    <message>
        <source>Checking board files...</source>
        <translation>正在检查板端文件…</translation>
    </message>
    <message>
        <source>Board files checked. Archived rollback copies on this computer.</source>
        <translation>板端文件检查完成，回滚副本已归档到本机。</translation>
    </message>
    <message>
        <source>Board files are ready.</source>
        <translation>板端文件已就绪。</translation>
    </message>
    <message>
        <source>Board file check failed. Check the device connection and try again.</source>
        <translation>板端文件检查失败，请确认设备连接后重试。</translation>
    </message>
</context>
<context>
    <name>RvDtofBoardStream</name>
    <message>
        <source>Invalid candidate stream size</source>
        <translation>候选数据流长度无效</translation>
    </message>
    <message>
        <source>Invalid candidate stream header</source>
        <translation>候选数据流头无效</translation>
    </message>
    <message>
        <source>Unexpected candidate stream payload</source>
        <translation>候选数据流载荷异常</translation>
    </message>
    <message>
        <source>Candidate identity or parameters mismatch</source>
        <translation>候选会话或参数不匹配</translation>
    </message>
    <message>
        <source>Invalid candidate measurement identity</source>
        <translation>候选测量标识无效</translation>
    </message>
    <message>
        <source>Invalid candidate depth values</source>
        <translation>候选深度数值无效</translation>
    </message>
    <message>
        <source>CXWL Depth Camera candidate stream disconnected</source>
        <translation>CXWL Depth Camera 候选数据流已断开</translation>
    </message>
    <message>
        <source>CXWL Depth Camera candidate stream timed out</source>
        <translation>CXWL Depth Camera 候选数据流超时</translation>
    </message>
    <message>
        <source>Invalid HD encoder progress or identity</source>
        <translation>高清视频编码进度或会话身份无效</translation>
    </message>
    <message>
        <source>Unsupported device calibration</source>
        <translation>不支持此设备标定</translation>
    </message>
</context>
<context>
    <name>RvImportDiagnosticsDialog</name>
    <message>
        <source>Close</source>
        <translation>关闭</translation>
    </message>
    <message>
        <source>CXWL Depth Camera import failed</source>
        <translation>CXWL Depth Camera 导入失败</translation>
    </message>
    <message>
        <source>The diagnostic report could not be saved.</source>
        <translation>无法保存诊断报告。</translation>
    </message>
    <message>
        <source>Send this diagnostic report to support:
%1</source>
        <translation>请将此诊断报告发送给运维人员：
%1</translation>
    </message>
    <message>
        <source>Open containing folder</source>
        <translation>打开所在文件夹</translation>
    </message>
    <message>
        <source>Diagnostic report folder</source>
        <translation>诊断报告文件夹</translation>
    </message>
    <message>
        <source>Diagnostic reports (*.txt)</source>
        <translation>诊断报告 (*.txt)</translation>
    </message>
    <message>
        <source>Save a copy...</source>
        <translation>另存副本…</translation>
    </message>
    <message>
        <source>The diagnostic report is no longer available.</source>
        <translation>诊断报告已不可用。</translation>
    </message>
    <message>
        <source>The diagnostic report copy could not be saved.</source>
        <translation>无法保存诊断报告副本。</translation>
    </message>
</context>
<context>
    <name>PointCloudRecorder</name>
    <message><source>A recording is already in progress.</source><translation>已有点云录制正在进行。</translation></message>
    <message><source>Cannot create the recording folder in %1.</source><translation>无法在 %1 创建录制目录。</translation></message>
    <message><source>The disk writer could not keep up with capture.</source><translation>磁盘写入速度不足，录制已提前结束。</translation></message>
    <message><source>Could not write %1.</source><translation>无法写入 %1。</translation></message>
    <message><source>Could not remove the empty recording folder.</source><translation>无法移除空录制目录。</translation></message>
    <message><source>Could not rename the completed recording folder.</source><translation>无法重命名已录制的目录。</translation></message>
</context>
<context>
    <name>RvPointCloudWidget</name>
    <message><source>Record point cloud</source><translation>录制点云</translation></message>
    <message><source>Stop recording and save</source><translation>停止录制并保存</translation></message>
    <message><source>Saving recording…</source><translation>正在保存录制…</translation></message>
    <message><source>Select point cloud recording folder</source><translation>选择点云录制目录</translation></message>
    <message><source>Also record depth</source><translation>同时录制深度</translation></message>
    <message><source>Also record RGB</source><translation>同时录制 RGB</translation></message>
    <message><source>No frames were received during recording.</source><translation>录制期间未收到数据帧。</translation></message>
    <message><source>Saved %1 point cloud frames and %2 RGB frames to %3</source><translation>已保存 %1 帧点云和 %2 帧 RGB 至 %3</translation></message>
    <message><source>Saved %1 frames to %2</source><translation>已保存 %1 帧至 %2</translation></message>
    <message>
        <source>Input angle (%1°)</source>
        <translation>输入角度（%1°）</translation>
    </message>
    <message>
        <source>Point cloud angle</source>
        <translation>点云角度</translation>
    </message>
    <message>
        <source>Clockwise angle (0–359°):</source>
        <translation>顺时针角度（0–359°）：</translation>
    </message>
    <message>
        <source>Rotate the point cloud clockwise around the ToF forward axis.</source>
        <translation>绕 ToF 前向轴顺时针旋转点云。</translation>
    </message>
    <message>
        <source>Cancel</source>
        <translation>取消</translation>
    </message>
    <message>
        <source>Points</source>
        <translation>点状</translation>
    </message>
    <message>
        <source>Mesh</source>
        <translation>网格</translation>
    </message>
    <message>
        <source>Point cloud display mode</source>
        <translation>点云显示方式</translation>
    </message>
    <message>
        <source>Fixed Z color range</source>
        <translation>固定 Z 色标</translation>
    </message>
    <message>
        <source>Dynamic + histogram equalization</source>
        <translation>动态＋直方图均衡</translation>
    </message>
    <message>
        <source>Point cloud color mode</source>
        <translation>点云着色模式</translation>
    </message>
    <message>
        <source>Equalize depths from 0.10 to 10.00 m in each frame. Colors can vary across frames.</source>
        <translation>逐帧均衡 0.10–10.00 m 的有效深度。同一深度的颜色可能随帧变化。</translation>
    </message>
    <message>
        <source>Reset view</source>
        <translation>重置视角</translation>
    </message>
    <message>
        <source>Z color range</source>
        <translation>Z 色标</translation>
    </message>
    <message>
        <source>Color minimum (m)</source>
        <translation>色标下限（米）</translation>
    </message>
    <message>
        <source>Color maximum (m)</source>
        <translation>色标上限（米）</translation>
    </message>
    <message>
        <source>Apply</source>
        <translation>应用</translation>
    </message>
    <message>
        <source>Point cloud view</source>
        <translation>点云视图</translation>
    </message>
    <message>
        <source>Drag to rotate, middle-drag to pan, and scroll to zoom.</source>
        <translation>左键拖动旋转，中键拖动平移，滚轮缩放。</translation>
    </message>
    <message>
        <source>Near</source>
        <translation>近</translation>
    </message>
    <message>
        <source>Far</source>
        <translation>远</translation>
    </message>
    <message>
        <source>%1 m</source>
        <translation>%1 米</translation>
    </message>
    <message>
        <source>3D view unavailable</source>
        <translation>三维视图不可用</translation>
    </message>
    <message>
        <source>Start capture in Depth Map or Camera raw video.</source>
        <translation>请在深度图或摄像头原视频中开始采集。</translation>
    </message>
    <message>
        <source>Waiting for a depth frame...</source>
        <translation>正在等待深度帧…</translation>
    </message>
</context>
<context>
    <name>RvSessionController</name>
    <message>
        <source>Starting camera and depth capture...</source>
        <translation>正在启动摄像头和深度采集…</translation>
    </message>
    <message>
        <source>Native ToF depth expired</source>
        <translation>原生 ToF 深度已过期</translation>
    </message>
    <message>
        <source>The previous board capture identity is unavailable. Reconnect from Device Manager.</source>
        <translation>无法确认上一轮板端采集的身份。请在设备管理中重新连接。</translation>
    </message>
    <message>
        <source>Automatically restoring camera and depth...</source>
        <translation>正在自动恢复相机和深度…</translation>
    </message>
    <message>
        <source>Automatic recovery stopped because the board could not safely restart.</source>
        <translation>板端未能安全重启，已停止自动恢复。</translation>
    </message>
    <message>
        <source>Click Start or Reconnect to attempt a controlled sensor reset. If recovery fails, the fault and ownership remain protected.</source>
        <translation>可点击“开始”或“立即重连”尝试复位传感器。恢复失败时仍保留故障和所有权保护。</translation>
    </message>
    <message>
        <source>No frames arrived for the selected board preset. Check board status.</source>
        <translation>未收到所选板端档位的帧，请检查板端状态。</translation>
    </message>
    <message>
        <source>Board preset switch timed out. Check board status.</source>
        <translation>切换板端档位超时，请检查板端状态。</translation>
    </message>
    <message>
        <source>Preview switch failed; check board status</source>
        <translation>分辨率切换失败，请检查板端状态</translation>
    </message>
    <message>
        <source>Stopped</source>
        <translation>已停止</translation>
    </message>
    <message>
        <source>Connecting</source>
        <translation>连接中</translation>
    </message>
    <message>
        <source>The stream is temporarily unavailable</source>
        <translation>视频流暂时不可用</translation>
    </message>
    <message>
        <source>Playing</source>
        <translation>播放中</translation>
    </message>
    <message>
        <source>Error</source>
        <translation>错误</translation>
    </message>
    <message>
        <source>Camera is not started</source>
        <translation>摄像头未启动</translation>
    </message>
    <message>
        <source>Switching work mode...</source>
        <translation>工作模式切换中…</translation>
    </message>
    <message>
        <source>Reconnecting</source>
        <translation>断线重连</translation>
    </message>
    <message>
        <source>%1; reconnect %2 in %3 ms</source>
        <translation>%1；%3 ms 后进行第 %2 次重连</translation>
    </message>
    <message>
        <source>Managed camera encoder progress stopped; check board status</source>
        <translation>受管相机编码进度停止，请检查板端状态</translation>
    </message>
    <message>
        <source>Disconnected / No data</source>
        <translation>未连接 / 无数据</translation>
    </message>
    <message>
        <source>Candidate receipt mismatch</source>
        <translation>板端候选回执不匹配</translation>
    </message>
    <message>
        <source>Candidate confirmation is missing</source>
        <translation>缺少板端候选确认</translation>
    </message>
    <message>
        <source>Waiting for CXWL Depth Camera candidate output</source>
        <translation>等待 CXWL Depth Camera 候选结果</translation>
    </message>
    <message>
        <source>Device selected; click Start when ready.</source>
        <translation>设备已选择，请按需点击“开始”。</translation>
    </message>
    <message>
        <source>Open Settings &gt; Device Manager to review this device.</source>
        <translation>请打开“设置 → 设备管理”核对该设备。</translation>
    </message>
    <message>
        <source>H.265 video and native %1×%2 ToF</source>
        <translation>H.265 视频与 %1×%2 原生 ToF</translation>
    </message>
    <message>
        <source>Required board files are missing.</source>
        <translation>板端缺少成像所需文件。</translation>
    </message>
    <message>
        <source>The board capture script is unavailable or unsupported.</source>
        <translation>板端采集脚本不可用或不受支持。</translation>
    </message>
    <message>
        <source>dToF Parameter Tuning</source>
        <translation>dToF 参数调试</translation>
    </message>
    <message>
        <source>Stop dToF tuning before selecting another device.</source>
        <translation>请先停止 dToF 调试，再选择其他设备。</translation>
    </message>
    <message>
        <source>CXWL Depth Camera is owned by dToF tuning. Stop it before starting shared imaging.</source>
        <translation>CXWL Depth Camera 正由 dToF 参数调试占用，请先停止调试再启动共享成像。</translation>
    </message>
    <message>
        <source>Connect a device using USB one-click connection in Device Manager.</source>
        <translation>请在「设置 → 设备管理」中使用「USB 一键连接」添加设备。</translation>
    </message>
    <message>
        <source>The board capture runtime is incomplete.</source>
        <translation>板端采集运行文件不完整。</translation>
    </message>
</context>
<context>
    <name>RvTunerController</name>
    <message>
        <source>Reading current native capture settings...</source>
        <translation>正在读取当前采集参数…</translation>
    </message>
    <message>
        <source>Device operation timed out; ownership and fault evidence are retained.</source>
        <translation>设备操作超时，保留归属和故障证据。</translation>
    </message>
    <message>
        <source>No fresh complete measurement; stopping the experiment.</source>
        <translation>没有新鲜完整测量，正在停止实验。</translation>
    </message>
    <message>
        <source>dToF Parameter Tuning</source>
        <translation>dToF 参数调试</translation>
    </message>
    <message>
        <source>Malformed device operation receipt.</source>
        <translation>设备操作回执格式错误。</translation>
    </message>
    <message>
        <source>Device identity changed; operation receipt rejected.</source>
        <translation>设备身份已变化，拒绝操作回执。</translation>
    </message>
    <message>
        <source>Device operation failed or timed out; check device logs.</source>
        <translation>设备操作失败或超时，请检查设备日志。</translation>
    </message>
    <message>
        <source>Checking parameters, installed runtime, device identity and actual capture owner...</source>
        <translation>正在核验参数、已安装运行库、设备身份及实际采集所有者…</translation>
    </message>
    <message>
        <source>Cancelled before handover; shared acquisition was left running.</source>
        <translation>接管前已取消，原共享采集保持运行。</translation>
    </message>
    <message>
        <source>Current settings read. Opening this module does not start capture.</source>
        <translation>当前参数已读取，打开模块不会自动采集。</translation>
    </message>
    <message>
        <source>Shared session changed during preflight; retry Start.</source>
        <translation>预检查期间共享会话已变化，请重新点击“开始”。</translation>
    </message>
    <message>
        <source>Stopping shared receivers. Waiting for the verified board owner to exit and acknowledge STOP...</source>
        <translation>正在停止共享接收，等待已核验的板端所有者退出并应答 STOP…</translation>
    </message>
    <message>
        <source>Experiment stopped. Shared imaging remains stopped; use Camera raw video &gt; Start.</source>
        <translation>实验已停止。普通成像保持停止，请从“摄像头原视频”手动开始。</translation>
    </message>
    <message>
        <source>Stopped</source>
        <translation>已停止</translation>
    </message>
    <message>
        <source>dToF tuning ended; start shared imaging manually.</source>
        <translation>dToF 调试已结束，请手动启动共享成像。</translation>
    </message>
    <message>
        <source>Experiment receipt differs from requested settings.</source>
        <translation>实验回执与请求参数不一致。</translation>
    </message>
    <message>
        <source>Experiment running. Changes are temporary until Long-term save is clicked.</source>
        <translation>实验正在运行。点击“长期保存”前的修改仅用于当前实验。</translation>
    </message>
    <message>
        <source>Applying experiment parameters...</source>
        <translation>正在应用实验参数…</translation>
    </message>
    <message>
        <source>Stopping the current experiment and verifying a new complete measurement...</source>
        <translation>正在停止当前实验并核验新参数的完整测量…</translation>
    </message>
    <message>
        <source>Backing up and atomically saving only dToF settings; verifying both scripts...</source>
        <translation>正在备份并原子保存 dToF 参数，核验两份采集脚本…</translation>
    </message>
    <message>
        <source>Stopping dToF tuning...</source>
        <translation>正在停止 dToF 调试…</translation>
    </message>
    <message>
        <source>Waiting for normal sensor STOP and resource release...</source>
        <translation>正在等待传感器正常 STOP 及资源释放…</translation>
    </message>
    <message>
        <source>Waiting for a complete DTN1 measurement...</source>
        <translation>正在等待完整 DTN1 测量…</translation>
    </message>
    <message>
        <source>dToF settings saved and read back. Imaging applies the selected session preset when capture starts.</source>
        <translation>dToF 参数已保存并回读验证。启动采集时应用所选的成像会话预设。</translation>
    </message>
</context>
<context>
    <name>RvTunerWidget</name>
    <message>
        <source>All supported resolutions can be saved permanently. Apply pending edits first. Both board sensor command lines are backed up and verified.</source>
        <translation>所有支持的分辨率均可长期保存。请先应用待修改参数。板端两处传感器命令均会备份并回读验证。</translation>
    </message>
    <message>
        <source>ID %1 | row %2, column %3 | target %4 | measurement %5
Range %6 mm (radial) | SNR %7 | slot %8 | Signal %9 | Noise encoded %10</source>
        <translation>原生 ID %1 | 行 %2，列 %3 | 目标 %4 | 测量 %5
距离 %6 mm（径向）| SNR %7 | 有效槽 %8 | Signal %9 | Noise 编码 %10</translation>
    </message>
    <message>
        <source>%1 counts, encoded 0x%2</source>
        <translation>%1 counts，编码值 0x%2</translation>
    </message>
    <message>
        <source>not enabled</source>
        <translation>未启用</translation>
    </message>
    <message>
        <source>Ordinary</source>
        <translation>普通</translation>
    </message>
    <message>
        <source>Long range</source>
        <translation>远距离</translation>
    </message>
    <message>
        <source>High accuracy</source>
        <translation>高精度</translation>
    </message>
    <message>
        <source>Dual</source>
        <translation>双模式</translation>
    </message>
    <message>
        <source>Dual long range</source>
        <translation>双模式远距离</translation>
    </message>
    <message>
        <source>not read / not applied</source>
        <translation>尚未读取／尚未应用</translation>
    </message>
    <message>
        <source>%1×%2 / mode %3 / SNR %4 / %5k / %6ms / near %7k / %8 target(s) / Signal %9 / Noise %10</source>
        <translation>%1×%2／模式 %3／SNR %4／%5k／%6ms／近距 %7k／%8 目标／Signal %9／Noise %10</translation>
    </message>
    <message>
        <source>on</source>
        <translation>开</translation>
    </message>
    <message>
        <source>off</source>
        <translation>关</translation>
    </message>
    <message>
        <source>Pending: %1</source>
        <translation>待应用：%1</translation>
    </message>
    <message>
        <source>Read the current device configuration first.</source>
        <translation>请先读取设备当前配置。</translation>
    </message>
    <message>
        <source>Experiment: %1</source>
        <translation>当前实验：%1</translation>
    </message>
    <message>
        <source>Long-term saved: %1</source>
        <translation>已长期保存：%1</translation>
    </message>
    <message>
        <source>Read the current device configuration first. Opening or restoring this dock does not start acquisition.</source>
        <translation>请先读取设备当前配置。打开模块或恢复布局不会自动采集。</translation>
    </message>
    <message>
        <source>Target %1</source>
        <translation>目标 %1</translation>
    </message>
    <message>
        <source>0 – %1 mm (radial)</source>
        <translation>0 – %1 mm（径向）</translation>
    </message>
    <message>
        <source>0 – %1 counts (%2)</source>
        <translation>0 – %1 counts（%2）</translation>
    </message>
    <message>
        <source>log</source>
        <translation>对数</translation>
    </message>
    <message>
        <source>linear</source>
        <translation>线性</translation>
    </message>
    <message>
        <source>Signal not enabled / no data. Enable Signal preview to compare both plots. Preview does not save permanent settings.</source>
        <translation>Signal 未启用／无数据。请启用 Signal 预览以比较双图。预览不会长期保存参数。</translation>
    </message>
    <message>
        <source>Valid %1/%2 (%3%) | SNR min/mean/max %4/%5/%6 | received %7 FPS | %8 °C | warnings 0x%9 | dropped %10</source>
        <translation>有效 %1/%2（%3%）| SNR 最小／均值／最大 %4/%5/%6 | 实收 %7 FPS | %8 °C | 告警 0x%9 | 丢弃 %10</translation>
    </message>
    <message>
        <source>Save experiment preset</source>
        <translation>保存实验预设</translation>
    </message>
    <message>
        <source>JSON files (*.json)</source>
        <translation>JSON 文件 (*.json)</translation>
    </message>
    <message>
        <source>Load experiment preset</source>
        <translation>加载实验预设</translation>
    </message>
    <message>
        <source>Export comparison results</source>
        <translation>导出比较结果</translation>
    </message>
    <message>
        <source>Read current</source>
        <translation>读取当前配置</translation>
    </message>
    <message>
        <source>Start</source>
        <translation>开始</translation>
    </message>
    <message>
        <source>Stop</source>
        <translation>停止</translation>
    </message>
    <message>
        <source>Apply experiment</source>
        <translation>应用实验参数</translation>
    </message>
    <message>
        <source>Long-term save</source>
        <translation>长期保存</translation>
    </message>
    <message>
        <source>Save preset</source>
        <translation>保存预设</translation>
    </message>
    <message>
        <source>Load preset</source>
        <translation>加载预设</translation>
    </message>
    <message>
        <source>Enable Signal preview</source>
        <translation>启用 Signal 预览</translation>
    </message>
    <message>
        <source>Record comparison</source>
        <translation>记录比较结果</translation>
    </message>
    <message>
        <source>Export results</source>
        <translation>导出结果</translation>
    </message>
    <message>
        <source>Resolution</source>
        <translation>分辨率</translation>
    </message>
    <message>
        <source>Mode</source>
        <translation>测量模式</translation>
    </message>
    <message>
        <source>Iterations (k)</source>
        <translation>累计次数（k）</translation>
    </message>
    <message>
        <source>SNR threshold</source>
        <translation>SNR 阈值</translation>
    </message>
    <message>
        <source>Period (ms)</source>
        <translation>测量周期（ms）</translation>
    </message>
    <message>
        <source>Dual near iterations (k)</source>
        <translation>双模式近距累计（k）</translation>
    </message>
    <message>
        <source>Targets per zone</source>
        <translation>每区域目标数</translation>
    </message>
    <message>
        <source>Signal output</source>
        <translation>输出 Signal</translation>
    </message>
    <message>
        <source>Noise output</source>
        <translation>输出 Noise</translation>
    </message>
    <message>
        <source>Log</source>
        <translation>对数</translation>
    </message>
    <message>
        <source>Native grid</source>
        <translation>原生网格</translation>
    </message>
    <message>
        <source>Aligned display</source>
        <translation>对齐显示</translation>
    </message>
    <message>
        <source>Both plots use the same complete measurement and target.</source>
        <translation>两图来自同一个完整测量和同一目标。</translation>
    </message>
    <message>
        <source>Distance display maximum; does not change measurements.</source>
        <translation>距离色标上限，不改变测量值。</translation>
    </message>
    <message>
        <source>Signal display maximum in decoded counts. Encoded values are shown on hover.</source>
        <translation>Signal 色标上限，单位为解码 counts。悬停可查看原始编码值。</translation>
    </message>
    <message>
        <source>Distance (radial)</source>
        <translation>距离图（径向）</translation>
    </message>
    <message>
        <source>Photon intensity (Signal counts)</source>
        <translation>光子强度图（Signal counts）</translation>
    </message>
    <message>
        <source>No measurement. Read configuration, then click Start.</source>
        <translation>暂无测量。请读取配置后点击“开始”。</translation>
    </message>
    <message>
        <source>Parameters</source>
        <translation>参数</translation>
    </message>
    <message>
        <source>Target</source>
        <translation>目标</translation>
    </message>
    <message>
        <source>Valid %</source>
        <translation>有效占比 %</translation>
    </message>
    <message>
        <source>Mean SNR</source>
        <translation>平均 SNR</translation>
    </message>
    <message>
        <source>Received FPS</source>
        <translation>实收 FPS</translation>
    </message>
    <message>
        <source>Measurement</source>
        <translation>测量序号</translation>
    </message>
</context>
<context>
    <name>RvUsbEnrollment</name>
    <message>
        <source>ADB was not found. Place adb.exe and its DLLs in the platform-tools folder next to the application.</source>
        <translation>未找到 ADB。请将 adb.exe 及其 DLL 放入程序旁的 platform-tools 文件夹。</translation>
    </message>
    <message>
        <source>Could not list USB devices with ADB.</source>
        <translation>无法通过 ADB 列出 USB 设备。</translation>
    </message>
    <message>
        <source>Could not read the CXWL Depth Camera identity over USB.</source>
        <translation>无法通过 USB 读取 CXWL Depth Camera 身份信息。</translation>
    </message>
    <message>
        <source>CXWL Depth Camera setup unavailable: %1</source>
        <translation>无法自动配置 CXWL Depth Camera：%1</translation>
    </message>
    <message>
        <source>Stop imaging before setting up the new CXWL Depth Camera.</source>
        <translation>请先停止成像，再配置新的 CXWL Depth Camera。</translation>
    </message>
    <message>
        <source>The CXWL Depth Camera connection changed. Reconnect the board and try again.</source>
        <translation>CXWL Depth Camera 的连接已变化，请重新插入设备后重试。</translation>
    </message>
    <message>
        <source>Could not configure the CXWL Depth Camera USB network adapter.</source>
        <translation>无法配置 CXWL Depth Camera 的 Windows USB 网卡。</translation>
    </message>
    <message>
        <source>Could not assign an IPv4 address to the CXWL Depth Camera.</source>
        <translation>无法为 CXWL Depth Camera 的 USB 网口设置 IPv4 地址。</translation>
    </message>
    <message>
        <source>CXWL Depth Camera verification failed. Check its factory login, calibration and USB connection.</source>
        <translation>CXWL Depth Camera 验证失败，请检查出厂账号、标定和 USB 连接。</translation>
    </message>
</context>
<context>
    <name>SensorMainWindow</name>
    <message>
        <source>Capture is unavailable.</source>
        <translation>采集暂不可用。</translation>
    </message>
    <message>
        <source>Could not start capture. Check the device connection and try again.</source>
        <translation>无法开始采集，请确认设备连接后重试。</translation>
    </message>
    <message>
        <source>USB connection setup ended.</source>
        <translation>USB 连接流程已结束。</translation>
    </message>
    <message>
        <source>Point Cloud</source>
        <translation>点云</translation>
    </message>
    <message>
        <source>The device needs board files before capture can start. Check and repair them now?</source>
        <translation>采集需要板端运行文件。现在检查并修复？</translation>
    </message>
    <message>
        <source>CXWL Depth Camera connection</source>
        <translation>CXWL Depth Camera 连接</translation>
    </message>
    <message>
        <source>A new CXWL Depth Camera was detected. Go to Settings to add it automatically?</source>
        <translation>检测到新的 CXWL Depth Camera，是否前往设置自动添加设备？</translation>
    </message>
    <message>
        <source>CXWL Depth Camera board files need updating. Go to Settings to update them automatically?</source>
        <translation>CXWL Depth Camera 的板端文件需要更新，是否前往设置自动更新？</translation>
    </message>
    <message>
        <source>The CXWL Depth Camera connection needs setup. Go to Settings to complete it automatically?</source>
        <translation>CXWL Depth Camera 连接配置尚未完成，是否前往设置自动完成配置？</translation>
    </message>
    <message>
        <source>About</source>
        <translation>关于</translation>
    </message>
    <message>
        <source>Depth Map</source>
        <translation>深度图</translation>
    </message>
    <message>
        <source>Camera raw video</source>
        <translation>摄像头原视频</translation>
    </message>
    <message>
        <source>File</source>
        <translation>文件</translation>
    </message>
    <message>
        <source>Exit</source>
        <translation>退出</translation>
    </message>
    <message>
        <source>View</source>
        <translation>视图</translation>
    </message>
    <message>
        <source>Reset layout</source>
        <translation>恢复默认布局</translation>
    </message>
    <message>
        <source>Layout Presets</source>
        <translation>布局预设</translation>
    </message>
    <message>
        <source>Save layout...</source>
        <translation>保存布局…</translation>
    </message>
    <message>
        <source>Delete layout...</source>
        <translation>删除布局…</translation>
    </message>
    <message>
        <source>Settings</source>
        <translation>设置</translation>
    </message>
    <message>
        <source>Device Manager...</source>
        <translation>设备管理…</translation>
    </message>
    <message>
        <source>Copy diagnostics</source>
        <translation>复制诊断</translation>
    </message>
    <message>
        <source>Diagnostics copied to the clipboard.</source>
        <translation>已将诊断信息复制到剪贴板。</translation>
    </message>
    <message>
        <source>Delete layout</source>
        <translation>删除布局</translation>
    </message>
    <message>
        <source>Layout name:</source>
        <translation>布局名称：</translation>
    </message>
    <message>
        <source>Save layout</source>
        <translation>保存布局</translation>
    </message>
    <message>
        <source>No saved layouts</source>
        <translation>尚无已保存布局</translation>
    </message>
    <message>
        <source>Install required board files</source>
        <translation>安装板端所需文件</translation>
    </message>
</context>
</TS>
