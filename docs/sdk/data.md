@page sdk_data 图像、测量质量与坐标

| 数据 | 约定 |
|---|---|
| 深度 | 8×8、16×16 或 48×32，F32_LE，径向距离，单位米，缺测为 0 |
| 点云 | GetFramePointCloud 返回与深度同尺寸、同序号、同时间戳的板端 XYZ。每像素 12 字节，小端 float32，缺测为 (0,0,0) |
| 视频 | 2304×1296，RGB8；按 row_stride_bytes 读取 |
| SNR | SubscribeEx 的 U8 图，传感器首峰 SNR，不是概率 |
| 有效图 | SubscribeEx 的 U8 图，0 无效、1 有效，与深度同序号和采集代次 |
| 时间 | 视频为主机单调时钟，深度为板端单调时钟，不可直接相减 |

帧回调借出的 `image.data`、`device_id_utf8`、`capture_generation_utf8`、`metadata_json_utf8` 及扩展质量图在回调返回时失效。延后使用时按行复制像素，并复制所需字符串。`*Alloc` 查询另行分配的快照可在句柄关闭后读取，最后调用 `CxwlDepth_FreeJson`。`CxwlDepthQuality` 示例演示把距离、SNR 和有效位复制到调用方缓冲区。

SDK 通过帧回调分别提供深度和视频数据，帧类型由 stream 字段标识。以帧中的 `image.width`、`image.height` 和 `image.row_stride_bytes` 读取像素。GetCapabilities 返回本 SDK 支持的六档模式。GetCapabilities 的 synchronized_rgb_depth 为 0。停止、换设备、换档和恢复会使此前的帧失效。收到清除事件或 `session_epoch` 变化时丢弃旧缓存。`capture_generation_utf8` 是不透明 UTF-8 标识，复制后按字节判等。同一代次内按 `stream` 分开比较 `sequence`，视频和深度各自编号。

## 标定与点云

RV1103 在采集到每个原生测量时计算有序 XYZ，并与同帧径向距离和质量数据一起发送。上位机显示和录制直接使用收到的 XYZ。传感器坐标系中 X 沿列号增加、Y 沿行号增加、Z 向前，单位为米。当前回调缺少匹配的板端点云时返回 UNAVAILABLE。延迟写盘应复制 XYZ 像素和同帧元数据。仅保存了径向深度的帧无法导出 PLY。实际使用的 F6 标定版本可查询 CxwlDepth_GetDeviceInfo 的 calibration_release_utf8。

### 读取实时点云

在深度回调中将当前帧传给 `CxwlDepth_GetFramePointCloud`。返回的点云按原始网格排列，缺测点为 (0, 0, 0)。下例放在 C 帧回调内，需包含 `<string.h>` 和 `<stdio.h>`。它打印有效中心像素的 XYZ，单位为米。扩展回调传入 `&extended->frame`。C++ 可用 `cxwl::frame_point_cloud(*frame)` 取得相同的视图。

```c
CxwlDepthFrame cloud = {0};
cloud.size = sizeof(cloud);
if (frame->stream == CXWL_DEPTH_STREAM_DEPTH &&
    CxwlDepth_GetFramePointCloud(frame, &cloud) == CXWL_DEPTH_OK) {
    const size_t x = cloud.image.width / 2, y = cloud.image.height / 2;
    const unsigned char *row = (const unsigned char *)cloud.image.data
                             + y * cloud.image.row_stride_bytes;
    CxwlDepthPoint p;
    memcpy(&p, row + x * sizeof(p), sizeof(p));
    if (p.z_m > 0) printf("%.3f %.3f %.3f m\n", p.x_m, p.y_m, p.z_m);
}
```

### 将深度帧导出为 PLY

在深度回调中调用 `CxwlDepth_SavePointCloudPly(frame, path_utf8)`，即可将板端同帧 XYZ 同步写成一个 PLY 文件。六个采集档位均可导出。路径用 UTF-8，父目录须已存在。成功写入会替换同名文件。C++ 对应函数为 `cxwl::save_point_cloud_ply(*frame, path_utf8)`。

连续录制时给每帧分配独立文件名。要录制 10 秒，可在应用中启动单调计时器，在回调内按行复制 XYZ，并保留网格、档位、采集代次、帧号、时间戳和时钟域。后台线程使用这些副本写盘。满 10 秒时停止加入新帧，等已入队的帧写完即可。C++ 可用 `cxwl::Image(view.image)` 复制回调内的像素。

@anchor sdk_recorded_ply
### 读取录制的 PLY 文件

Viewer 的 `frame-00000001.ply` 等文件各保存一帧，可用点云查看器打开。坐标原点是 ToF 传感器，X 沿原始网格列号增加，Y 沿行号增加，Z 向前，单位为米。Viewer 的旋转、缩放和色标只影响预览。

| 文件头字段 | 含义 |
|---|---|
| `element vertex N` | 实际保存的有效点数。N 为 0 表示这一帧全部缺测。 |
| `width`、`height` | 原始网格的列数、行数。点按行保存并跳过缺测格，仅凭 PLY 无法还原缺测格的位置。 |
| `mode` | 1、2 为 48×32，3、4 为 8×8，5、6 为 16×16。每组依次为普通、高精度。 |
| `session_epoch`、`sequence` | 采集代次和设备帧号。可用同一代次内的帧号检查连续性。文件名中的编号表示保存顺序。 |
| `timestamp_ns`、`clock_domain` | 测量时间和时钟域。时钟域 2 表示板端单调时钟。同一代次两帧的时间戳之差除以十亿，即为间隔秒数。 |

在录制目录中将以下代码保存为 `read_cloud.py`，运行 `python read_cloud.py`。它用 Python 3 标准库读取文本头，按小端格式解析 `end_header` 行后的数据。每个点依次占用三个 float32，共 12 字节。

```python
from pathlib import Path
from struct import iter_unpack

for path in sorted(Path(".").glob("frame-*.ply")):
    header, data = path.read_bytes().split(b"end_header\n", 1)
    lines = header.decode("ascii").splitlines()
    if lines[:2] != ["ply", "format binary_little_endian 1.0"]:
        raise ValueError(f"Unexpected PLY format: {path.name}")
    count = int(next(s.split()[2] for s in lines
                     if s.startswith("element vertex ")))
    meta = dict(s[8:].split(" ", 1) for s in lines if s.startswith("comment "))
    if len(data) != count * 12:
        raise ValueError(f"Unexpected XYZ byte count: {path.name}")
    points = list(iter_unpack("<fff", data))
    print(path.name, meta["sequence"], len(points), points[:1])
```

每行输出文件名、设备帧号、有效点数和首个 XYZ。空帧的首点列表为 `[]`。如果同时录制深度，`depth/` 中的同编号文件保留完整的径向深度网格。选录 RGB 时，`rgb/` 保存独立编号的 JPEG，可用 `frames.csv` 的 `host_elapsed_ns` 查找接收时间最接近的画面。

## 深度与 RGB 的对齐条件

SDK 从相机接收 ToF 坐标系的 XYZ。将点云投到 RGB 像素还需要与当前图像几何匹配的 RGB 内参、畸变参数和 ToF 到 RGB 的外参，由应用另行标定。两路序号独立，时间戳属于不同的时钟域。近似配对可在同一主机单调时钟记录两路回调到达时间，并评估传输与解码延迟；需要曝光时刻对齐时须另有同步依据。

设备信息来自选中档案，profile_ready 表示设备档案的连接准备状态。实时连接状态通过 GetSessionStatus 查询。

@include DepthQuality.c
