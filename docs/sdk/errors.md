@page sdk_errors 状态、错误与排查

先区分同步返回码、异步会话状态与设备维护结果，再决定修正参数、重新连接还是恢复设备。
## 返回码与动作

| 返回码 | 常见触发 | 应用动作 | 自动重试 |
|---|---|---|---|
| OK (0) | 同步成功或异步请求已受理 | 异步操作继续派发事件，并查询对应状态 | 不适用 |
| INVALID_ARGUMENT (1) | 空指针、失效句柄、模式／路径冲突、在已有宿主事件循环中调用 Poll 或 WaitForEvent | 修正调用，使用有效句柄 | 否 |
| BUSY (2) | 尚在操作、回调中控制或释放未完成 | 回调返回后继续派发；Disconnect/Close 要保留句柄和上下文直到 OK | 可，有界业务等待；退出清理继续完成 |
| WRONG_THREAD (3) | 调用线程不是句柄所有者 | 将操作投递回所有者线程；C/C++ 无需创建 Qt 对象 | 否 |
| DEVICE_NOT_FOUND (4) | 目标不存在、档案未就绪或 USB 检查 ID 过期 | 重新枚举／扫描，再明确选择目标 | 条件修复后 |
| BUFFER_TOO_SMALL (5) | JSON 容量不足或输出结构 size 过小 | JSON 按返回容量重新分配；结构体设 size=sizeof(type) | 容量修复后 |
| UNAVAILABLE (6) | 初始化／内部操作失败、离线保护、USB 登记无法受理 | 立即 GetLastError；再读状态诊断 | 先定位原因 |

每个函数的具体返回码以头文件为准。公共异常边界与日志回调重入规则在返回码枚举中说明。GetLastError 保存本线程最近一次同步失败，成功调用不清除；异步失败可能没有新的 last error，必须读取结构化状态。

## 按会话错误类别处置

| error_category | 典型含义 | 处理 |
|---|---|---|
| NONE (0) | 无会话错误 | 结合 state 和帧可用位判断 |
| USB (1) | USB 网卡、设备证书核对或准备失败 | 检查 USB 数据线和设备管理器；运行诊断脚本；保存脱敏的 usb_error |
| BOARD_FILES (2) | 板端文件需要修复 | 先 Disconnect 至 OK，再 MaintainBoardFiles(BOARD_REPAIR)，等待维护结果后连接 |
| CAPTURE (3) | 采集启动、视频或深度恢复失败 | 保存 GetStateAlloc 快照，确认网络和供电，再在空闲后 Reconnect |

## 设备故障恢复

设备报告故障且重连未恢复时，使用 CxwlDepth_RecoverDevice。Viewer 的“设备管理 → 恢复设备”和 C 示例的 recover 命令使用同一接口。

1. 调用 Disconnect，派发事件直到返回 OK，释放采集句柄。
2. 用 ListSavedDevicesAlloc 取得目标设备 ID，调用 CxwlDepth_RecoverDevice(manager, id)。OK 表示请求已受理。
3. 继续 PollDeviceManager，或使用宿主事件循环。收到 CXWL_DEPTH_EVENT_RECOVERY_CHANGED 后，用 GetDeviceManagerStateAlloc 查询 recovery 对象。
4. recovery.state 为 ready 时，用 ConnectWithOptions 连接该已登记设备并启动所需模式；为 error 时读取 recovery.error。

恢复操作保存故障证据、核对设备及采集所有权、清理故障状态并准备配套运行文件。存在活动采集客户端或未释放的设备资源时，结果会给出相应原因。recovery.board_archive 为板端证据目录，cleared_fault_count 为本次清理的故障标记数。

```powershell
./Example/bin/CxwlDepthDeviceManagement.exe list
$deviceId = Read-Host '请输入上一条 list 输出中目标设备的 id'
./Example/bin/CxwlDepthDeviceManagement.exe recover $deviceId
```

## 状态转换

```text
Open -> STOPPED --Start--> CONNECTING --两条流就绪--> STREAMING
                            |                         |
                         失败 v                    断流 v
                           ERROR <---------------- RECOVERING
                            |                         |
                            +--重连/条件修复----------+--> CONNECTING
任意活动状态 --Stop--> STOPPING --> STOPPED（句柄仍有效）
任意状态 --Disconnect/Close--> STOPPING --OK--> 句柄失效
```

connection_stage 是连接准备阶段，不是 state：IDLE=0、CHECKING=1、STARTING=3、WAITING_FOR_DEPTH=4；2 为 ABI 保留值。单纯收到一个深度帧不代表两条流已就绪。切换采集模式、停止或断流清除事件会撤销旧帧的可用性。

## 首次使用与支持材料

首次启动且没有登记设备时，Viewer 显示等待接入。Windows 识别相机的 RNDIS 网卡后，选择「USB 一键连接」并核对相机。SDK 验证预装服务并登记，采集就绪后出现深度图、RGB 视频和点云。示例最多等待 360 秒。若提示固件不匹配，请核对相机所配的服务和 SDK 版本。

反馈问题时附上 Viewer 状态栏「复制诊断」得到的 JSON、`SelfTest -Report` 报告和严格校验结果。同步失败记录 SDK 返回码，异步失败记录当时的状态、阶段和错误类别。发送前请移除凭据、私钥及设备档案内容。

SDK 按 Windows 用户管理默认设备档案并沿用已有登记。应用显式传入 `CxwlDepthOptions.device_store_utf8` 时，档案保存在该路径。Viewer 的界面偏好使用 `$env:LOCALAPPDATA/CXWL/DepthSdkViewer/`，设备凭据由 Windows 凭据管理器保存。`RemoveSavedDevice` 只移除所选登记记录。需重置默认档案时先退出所有 SDK／Viewer 程序，由支持人员指导备份和重置。应用分发包应省略设备档案。


## 收集崩溃现场

程序挂起但仍在运行时，可在 Windows 任务管理器的“详细信息”中右键实际进程（如 `CxwlDepthViewer.exe`），选择创建内存转储，记录生成路径。对于启动后立即崩溃，可由管理员按 [Windows Error Reporting LocalDumps](https://learn.microsoft.com/en-us/windows/win32/wer/collecting-user-mode-dumps) 为该 exe 单独配置 `DumpType=2`，默认目录为 `$env:LOCALAPPDATA/CrashDumps`；若此前没有该程序的 LocalDumps 配置，可在管理员 PowerShell 中运行以下命令。已有配置时先记录原 DumpType，复现后恢复原值。

```powershell
$key = 'HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\CxwlDepthViewer.exe'
reg.exe add $key /v DumpType /t REG_DWORD /d 2 /f
# 完成采集后移除本次新增的值
reg.exe delete $key /v DumpType /f
```

一并保存 SDK 版本、`sdk-manifest.json`、出错时间、日志和复现步骤。dump 可能包含相机帧、内存与凭据，只通过双方确认的支持渠道交付。客户包不包含核心 PDB；支持方需用与该 DLL 精确匹配的内部符号解析，不能使用其他构建的 PDB。
