# 项目结构

| 位置 | 用途 |
|---|---|
| `sdk/`、`sdk/examples/` | ABI v3、C++17 封装与客户示例 |
| `sdk/windows/` | RNDIS 接口选择、证书核验、会话、维护与媒体通道 |
| `demo/`、`sensors/` | Viewer、点云录制、采集控制与显示 |
| `tools/rgb_calibration/` | 已登记相机的独立 RGB 校正程序与估计算法 |
| `tools/rv_capture/`、`tools/rv_photon/` | 采集协议、原生网格与传感器数据类型 |
| `private_build/windows-device-service/` | 本地 RV1103 服务、固定板端运行输入、工厂配置与验收工具 |
| `tools/sdk_package/` | 客户 SDK 暂存、部署与示例构建模板 |

六个公开档位 1–6 对应板端 24、25、30、31、32、33。相机通过 HELLO 报告支持档位，START 返回采集代次与 F6 标定回执。读取帧时使用实际图像尺寸和行跨度。
