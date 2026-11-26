@page sdk_specifications 产品信息与测量规格

本页区分软件接口契约、传感器典型指标与历史实验室实测，选型时应同时查看每项条件。
## 产品版本

| 项目 | 内容 |
|---|---|
| 产品 | RGBD 深度相机 |
| SDK | Windows 3.0.0 开发版（ABI v3） |
| 本地预览 | Windows x64，设备服务 1.0.0-preview.1。 |

@anchor sdk_depth_presets

## 原生深度档位与测距范围

使用下表的 SDK 常量连接或切档。深度、SNR、有效位和板端 XYZ均按所选原生网格逐行排列。

| 档位 | SDK 常量和值 | 板端预设与驱动参数 | 预设标称上限 |
|---|---|---|---:|
| 48×32 普通 | `CXWL_DEPTH_BOARD = 1` | mode24，`-d 11` | 5.7 m |
| 48×32 高精度 | `CXWL_DEPTH_BOARD_HIGH_ACCURACY = 2` | mode25，`-d 12` | 1.4 m |
| 8×8 普通 | `CXWL_DEPTH_BOARD_8X8 = 3` | mode30，`-d 0` | 5.7 m |
| 8×8 高精度 | `CXWL_DEPTH_BOARD_8X8_HIGH_ACCURACY = 4` | mode31，`-d 2` | 5.7 m |
| 16×16 普通 | `CXWL_DEPTH_BOARD_16X16 = 5` | mode32，`-d 5` | 5.7 m |
| 16×16 高精度 | `CXWL_DEPTH_BOARD_16X16_HIGH_ACCURACY = 6` | mode33，`-d 6` | 1.4 m |

上限来自厂商预设表。8×8 高精度提高近距精度，标称上限为 5.7 m。厂商从 20 cm × 26 cm、18% 反射率灰卡测得典型最小探测距离为 10 mm。厂商还在 350 lux 荧光照明下，用 16×16 高精度档扫描 10 mm–1 m，图中比较 600K 和 1800K 积分。可用距离取决于目标反射率、照明、积分、SNR 阈值及像素位置。 [TMF8829 数据手册表 6](https://look.ams-osram.com/m/4215c1ea1a30e283/original/TMF8829-Time-of-flight-sensor.pdf#page=25) · [表 5 与图 3](https://look.ams-osram.com/m/4215c1ea1a30e283/original/TMF8829-Time-of-flight-sensor.pdf#page=17)

@anchor sdk_validation_scope

## 版本兼容矩阵与验收范围

| 主机与接口 | 相机与验证范围 |
|---|---|
| Windows x64，SDK 3.0.0，ABI v3 | RV1103 设备服务 1.0.0-preview.1，SC3336 与 TMF8829 F6。SDK 提供六个采集档位，Viewer 提供两个 48×32 档位。 |
| Linux x64 与 ARM64 | 这两个平台的设备服务客户端待适配。 |

实机验收比较导出 PLY 与相机传回的同帧 XYZ，并检查有效深度、视频和会话释放。表中的标称测距指标来自传感器数据手册。

## 普通 48×32 模式的典型有效探测距离

下表取自 TMF8829 数据手册的 48×32 普通模式典型值。传感器测距测试使用 66 ms 周期、1200k 积分迭代，目标处为 350 lux 荧光照明。SDK 2.0.0 的历史帧率实验另用 600k。600k 和 1200k 分别表示约 60 万次、120 万次积分迭代。寄存器每单位对应 1024 次迭代。1.5 m × 1.5 m 的测试目标垂直于被测区域。反射率在 940 nm 处定义。

| 目标 | 中心像素 | 边角像素 |
|---|---|---|
| 90% 白卡 | 2.7 m | 1.9 m |
| 18% 灰卡 | 1.2 m | 0.8 m |

中心与边角分别按手册给出的像素位置读取。目标反射率、环境光、入射角、遮挡和安装都会影响现场结果。 [TMF8829 数据手册 §5.1](https://look.ams-osram.com/m/4215c1ea1a30e283/original/TMF8829-Time-of-flight-sensor.pdf#page=16)

视频和深度使用不同单调时钟域，不能直接相减作为链路延迟。测延迟需使用共同事件及同一时钟的端到端记录；从连续深度帧时间戳可计算深度测量间隔，从会话状态可读取 video_fps。

## 精度与坐标

准确度描述相对参考距离的偏差，重复性描述静态重复采样的分散程度。SDK 使用板端生成、与深度帧对应的 ToF 坐标系米制 XYZ。X 沿列号增加，Y 沿行号增加，Z 向前。


