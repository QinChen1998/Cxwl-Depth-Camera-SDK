@page sdk_terminology 术语与模式映射

应用传入公开 SDK 常量，由 SDK 选择匹配的板端预设和驱动参数。完整映射见本手册的「原生深度档位与测距范围」。

| 中文／English | 对应值或含义 |
|---|---|
| 普通模式 / Standard depth mode | 选用对应分辨率的默认测距预设。 |
| 高精度模式 / High accuracy depth mode | 选用对应分辨率的近距高精度预设。 |
| 启动器 / Launcher | Windows 包在 `Example/bin` 提供示例启动器。 |
| 运行依赖 / Runtime dependencies | 随包运行库包括 SDK、Qt 网络组件和对应部署档位的图像及视频库。 |
| 径向距离 / Radial distance | 从 ToF 传感器沿该像素射线到目标的距离，单位米；不等同于光轴 Z。 |
| F6 标定 / Calibration preset F6 | 六档共用的配套相机几何与视场校正方案代号。 |
| `dtof_fov_correction` | 诊断元数据中的 ToF 视场校正选择值；本产品为 6，对应 F6。应用无需自行传板端参数。 |
| SNR / Signal-to-noise ratio | 传感器报告的首峰信噪比。使用同帧质量图时，按像素检查 `valid_mask` 为 1 且径向距离大于 0。 |
| 采集代次 / Capture generation | 区分重新启动／恢复前后的采集；配合 `session_epoch` 拒绝旧帧。 |
| 原视频 / Camera video | 板端发送 H.265，SDK 主机解码为 RGB8；与深度为独立数据流。 |

| RNDIS / Remote Network Driver Interface Specification | Windows 主机通过 USB 网卡与相机通信的接口。 |
| CX2 | 相机 USB 网卡上的控制与媒体传输协议，端点为 `172.32.0.93:8943/TCP`。 |
| dToF / direct time of flight | 直接飞行时间测距。TMF8829 是相机的 dToF 传感器。 |
| RV1103、SC3336 | RV1103 为板端处理器，SC3336 为可见光图像传感器。 |
| LSG2 | SDK 与板端交换原生测量数据的协议标识。 |
| PDB / Program Database | 用于还原匹配 Windows 二进制调用栈的调试符号文件。 |
| ABI / Application binary interface | 二进制调用与结构布局契约。本版为 ABI v3。 |
| 分段时延 / Bounded segment latency | 板端完整帧发布至 SDK 回调的时间区间估计，不包含完整光学成像链路。 |
