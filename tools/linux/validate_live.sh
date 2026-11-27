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
cloud_ranges = [math.hypot(*point) for point in points]
assert all(math.isfinite(distance) and distance > 0 for distance in cloud_ranges)
# Recover the native quarter-millimetre count before testing the PGM zero bin.
# The norm of a float32 XYZ point at exactly 0.5 mm can fall on either side of it.
pgm_ranges = [distance for distance in cloud_ranges if round(distance * 4000) >= 2]
assert f"element vertex {len(points)}\n" in header and len(pgm_ranges) == len(radial_mm) > 0
error = max(abs(distance - mm / 1000) for distance, mm in zip(pgm_ranges, radial_mm))
assert error < 0.00051
assert "Owned depth 48x32, 1536 points" in (output / "cpp.log").read_text(encoding="utf-8")
result = {"passed": True, "pointcloud_valid_points": len(points), "pgm_nonzero_points": len(radial_mm),
          "pgm_rounded_to_zero_points": len(points) - len(pgm_ranges), "cpp_points": 1536, "max_radial_error_m": error}
(output / "samples.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
print(json.dumps(result))
PY
