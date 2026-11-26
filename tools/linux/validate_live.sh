#!/bin/bash
set -euo pipefail
umask 077
root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
stage=$(realpath "${1:?Provide the validation SDK directory}")
output=$(realpath -m "${2:?Provide a new output directory}")
seconds=${3:-12}
[ "${RV_TEST_OFFLINE_ONLY:-}" != 1 ]
mkdir "$output"
export XDG_DATA_HOME="${CXWL_VALIDATION_DATA_HOME:-$output/user-data}"
unset LD_LIBRARY_PATH QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH
examples="$stage/Example/bin"
registry="$XDG_DATA_HOME/DepthCameraAndTactileSensor/config/devices.json"
"$examples/CxwlDepthDeviceManagement" scan > "$output/scan.private.json" 2> "$output/scan.log"
if [[ ! -s $registry ]]; then
    "$examples/CxwlDepthDeviceManagement" connect-usb > "$output/enrollment.private.log" 2>&1
fi
test -s "$registry"
python3 -B -u "$root/tools/validate_sdk_live_outputs.py" --runtime "$stage/SDK/lib" \
    --registry "$registry" --output "$output/camera" --all-modes --transitions --seconds "$seconds"

mkdir "$output/pointcloud"
"$examples/CxwlDepthPointCloud" -Out "$output/pointcloud" > "$output/pointcloud.log" 2>&1
"$examples/CxwlDepthCppQuickStart" > "$output/cpp.log" 2>&1
python3 -B - "$output" <<'PY'
import json
import math
from pathlib import Path
import struct
import sys

output = Path(sys.argv[1])
with (output / "pointcloud/depth.pgm").open("rb") as depth:
    assert depth.readline().strip() == b"P5"
    assert depth.readline().startswith(b"#")
    assert tuple(map(int, depth.readline().split())) == (48, 32)
    assert int(depth.readline()) == 65535
    pixels = depth.read()
assert len(pixels) == 48 * 32 * 2
radial_mm = [value for (value,) in struct.iter_unpack(">H", pixels) if value]
ply = (output / "pointcloud/cloud.ply").read_text(encoding="ascii")
header, body = ply.split("end_header\n", 1)
points = [tuple(map(float, line.split())) for line in body.splitlines()]
assert f"element vertex {len(points)}\n" in header and len(points) == len(radial_mm) > 0
error = max(abs(math.sqrt(sum(v * v for v in point)) - mm / 1000) for point, mm in zip(points, radial_mm))
assert error < 0.00051
assert "Owned depth 48x32, 1536 points" in (output / "cpp.log").read_text(encoding="utf-8")
result = {"passed": True, "pointcloud_valid_points": len(points), "cpp_points": 1536, "max_radial_error_m": error}
(output / "samples.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
print(json.dumps(result))
PY
