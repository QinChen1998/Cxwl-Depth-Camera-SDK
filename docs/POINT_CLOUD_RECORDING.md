# 点云录制数据读取说明

一段录制是一组按时间排序的三维快照。打开录制目录，找到 `frame-00000001.ply`。用支持 PLY 的点云查看器打开它，即可查看第一帧。要批量读取坐标，请使用下一页的 Python 示例。

## 一个点代表什么

每个点给出一个测距位置在 ToF 深度传感器坐标系中的三维坐标，原点位于传感器，**单位为米**。文件头用 `coordinates tof_sensor metres` 标明这一约定。录制保存传感器原始朝向。上位机的输入角度、视角、缩放和色标只影响预览。

| 坐标 | 含义 |
|---|---|
| x | 横向位置，沿传感器原始网格列号增加的方向为正。 |
| y | 纵向位置，沿传感器原始网格行号增加的方向为正。 |
| z | 前向深度，从传感器向前为正。 |

例如，点 `(0.12, -0.04, 0.80)` 位于传感器前方 80 厘米，横向沿正 X 偏移 12 厘米，纵向沿负 Y 偏移 4 厘米。

## 一帧文件里有什么

每个 PLY 文件保存一帧。文件以可读的文本头开始，`end_header` 行之后是有效点的二进制数据。格式为 PLY 1.0 小端，每点按 x、y、z 顺序写入三个 32 位浮点数，共 12 字节。

| 文件头字段 | 如何理解 |
|---|---|
| element vertex N | 实际保存的有效点数。N 为 0 表示这一帧没有有效测距。 |
| width、height | 原始测距网格的列数和行数。点按从上到下、每行从左到右的顺序保存，跳过缺测格。仅凭 XYZ 列表无法确定缺测格的位置。 |
| mode | 采集档位。1、2 对应 48×32，3、4 对应 8×8，5、6 对应 16×16。每组前者为普通档，后者为高精度档。 |
| session_epoch、sequence | 采集代次与设备测量帧号。可在同一代次内用帧号跟踪测量。文件名中的数字只表示本次录制的保存顺序。 |
| timestamp_ns、clock_domain | 测量时间，单位纳秒。时钟域 2 表示板端单调时钟，数值从设备的计时起点递增。同一代次的两帧时间戳相减，再除以十亿，即为间隔秒数。 |

---

## 用 Python 读取一段录制

将下面的代码保存为 `read_cloud.py`，放在与 `frame-00000001.ply` 相同的目录。在该目录打开终端，运行 `python read_cloud.py`。示例仅使用 Python 3 标准库。

```python
from pathlib import Path
from struct import iter_unpack


def read_frame(path):
    header, data = path.read_bytes().split(b"end_header\n", 1)
    lines = header.decode("ascii").splitlines()
    count = int(next(line.split()[2] for line in lines
                     if line.startswith("element vertex ")))
    meta = dict(line[8:].split(" ", 1) for line in lines
                if line.startswith("comment "))
    if len(data) != count * 12:
        raise ValueError(f"Incomplete point data: {path.name}")
    return meta, list(iter_unpack("<fff", data))


for path in sorted(Path(".").glob("frame-*.ply")):
    meta, points = read_frame(path)
    print(path.name, meta["sequence"], len(points), points[:1])
```

每行依次打印文件名、设备帧号、有效点数和第一个点。变量 `points` 包含该帧的全部 `(x, y, z)` 元组。若有效点数为 0，最后一项显示 `[]`，程序会继续读取下一帧。

## 随录制提供的其他数据

| 可选内容 | 与点云的关系 |
|---|---|
| depth/ | 米制径向深度，表示传感器到测量点的直线距离。每个 .f32 文件对应同编号 PLY，保留完整网格，缺测值为 0。 |
| rgb/ | 彩色相机画面，每帧一张 JPEG，保留相机的原始画面方向。RGB 独立编号，帧率可与点云不同。 |
| frames.csv | 随 RGB 提供的时间索引。host_elapsed_ns 为上位机从开始录制到收到该帧的纳秒数，可用来寻找时间接近的画面。两路画面的曝光时刻可能不同。 |
