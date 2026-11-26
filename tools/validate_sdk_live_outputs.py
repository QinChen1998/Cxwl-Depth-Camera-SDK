"""Validate capture modes on a registered camera using the distributed shared library.

Pass a private copy of the selected device registry. Output includes local scene data.
Requires NumPy and Pillow. Use a fresh output directory for each physical run.
"""
import argparse
import ctypes as c
from datetime import datetime, timezone
import hashlib
import json
import os
import platform
from pathlib import Path
import time

import numpy as np
from PIL import Image as Picture

from measure_sdk_timing import Frame, Image, Options


# Public mode, output directory, dimensions, board preset and TMF8829 sensor mode.
MODES = (
    (1, "board", 48, 32, 24, 11), (2, "high", 48, 32, 25, 12),
    (3, "8x8", 8, 8, 30, 0), (4, "8x8-high", 8, 8, 31, 2),
    (5, "16x16", 16, 16, 32, 5), (6, "16x16-high", 16, 16, 33, 6),
)


class ExtendedFrame(c.Structure):
    _fields_ = [("size", c.c_uint32), ("frame", Frame), ("snr", Image), ("mask", Image)]


class Status(c.Structure):
    _fields_ = [(name, c.c_uint32) for name in (
        "size", "state", "error", "mode", "stage", "busy", "video", "depth", "retaining",
        "switch_active", "switch_ms", "board_files")] + [
        ("epoch", c.c_uint64), ("fps", c.c_double), ("device", c.c_char * 128), ("board_device", c.c_char * 128)]


class Version(c.Structure):
    _fields_ = [(name, c.c_uint32) for name in ("size", "abi", "major", "minor", "patch")] + [
        ("version", c.c_char * 32), ("build", c.c_char * 64)]


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def check(result, name):
    require(result == 0, f"{name} returned {result}")


def write_json(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def plane(image, width, height, fmt, dtype, channels=1):
    row_bytes = width * channels * np.dtype(dtype).itemsize
    require((image.width, image.height, image.format) == (width, height, fmt), "Unexpected image dimensions or format")
    span = (height - 1) * image.stride + row_bytes
    require(image.data and image.stride >= row_bytes and image.bytes >= span, "Invalid image pointer, stride or length")
    storage = (c.c_ubyte * span).from_address(image.data)
    rows = np.ndarray((height, width * channels), dtype=dtype, buffer=storage,
                      strides=(image.stride, np.dtype(dtype).itemsize))
    return rows.reshape(height, width, channels) if channels != 1 else rows


class SDK:
    def __init__(self, runtime):
        if os.name == "nt":
            self.directory = os.add_dll_directory(str(runtime))
            self.codecs = [c.CDLL(str(runtime / name)) for name in
                           ("avutil-59.dll", "swresample-5.dll", "avcodec-61.dll", "avformat-61.dll", "swscale-8.dll")]
        self.path = runtime / ("CxwlDepthSdk.dll" if os.name == "nt" else "libCxwlDepthSdk.so.3")
        dll = c.CDLL(str(self.path))
        self.dll = dll
        handle = c.c_void_p
        uint = c.c_uint32
        self.callback = c.CFUNCTYPE(None, handle, c.POINTER(ExtendedFrame))
        signatures = {
            "AbiVersion": (), "GetVersionInfo": (c.POINTER(Version),),
            "ConnectWithOptions": (c.POINTER(Options), handle, handle, handle, c.POINTER(handle)),
            "SubscribeEx": (handle, self.callback, handle, handle), "GetSessionStatus": (handle, c.POINTER(Status)),
            "WaitForEvent": (handle, uint, c.POINTER(uint)),
            "Stop": (handle,), "Disconnect": (handle,),
            "GetFramePointCloud": (c.POINTER(Frame), c.POINTER(Frame)),
            "SavePointCloudPly": (c.POINTER(Frame), c.c_char_p),
            "SetMode": (handle, uint), "Reconnect": (handle,), "Start": (handle,),
            "GetStateAlloc": (handle, c.POINTER(handle), c.POINTER(uint)), "FreeJson": (handle,)}
        for name, types in signatures.items():
            function = getattr(dll, "CxwlDepth_" + name)
            function.argtypes = types
            function.restype = c.c_int32
            setattr(self, name, function)

    def dispatch(self, session):
        delivered = c.c_uint32()
        check(self.WaitForEvent(session, 35, c.byref(delivered)), "WaitForEvent")

    def save_failure(self, session, destination):
        pointer, count = c.c_void_p(), c.c_uint32()
        if self.GetStateAlloc(session, c.byref(pointer), c.byref(count)) == 0:
            try:
                destination.write_bytes(c.string_at(pointer, count.value - 1))
            finally:
                check(self.FreeJson(pointer), "FreeJson")


def capture(sdk, runtime, registry, output, preset, seconds):
    output.mkdir()
    mode, name, width, height, candidate, sensor = preset
    result = {"mode": name, "width": width, "height": height, "sensor_mode": sensor, "candidate_mode": candidate,
              "rgb_frames": 0, "depth_frames": 0, "quality_frames": 0, "board_clouds": 0}
    generations = set()
    last = {}
    snapshot = {}
    errors = []
    rgb_levels = set()
    valid_counts, snr_min, snr_max = [], 255, 0
    max_error = 0.
    collecting = True
    streaming = None

    @sdk.callback
    def frame_callback(_, ptr):
        nonlocal snr_min, snr_max, max_error
        if not collecting or errors:
            return
        try:
            ex = ptr.contents
            f = ex.frame
            require(ex.size >= c.sizeof(ExtendedFrame) and f.size >= c.sizeof(Frame), "Frame size mismatch")
            require(f.mode == mode and f.stream in (1, 2) and f.generation and f.device, "Frame identity missing or wrong")
            generations.add((f.epoch, f.generation, f.device))
            if f.stream in last:
                seq, stamp = last[f.stream]
                require(f.sequence > seq and f.stamp > stamp, "Non-monotonic sequence or timestamp within one stream")
            last[f.stream] = (f.sequence, f.stamp)
            if f.stream == 1:
                require(f.coordinate == 0 and f.clock == 1 and not f.metadata and not ex.mask.data and not ex.snr.data,
                        "Unexpected video metadata or depth quality")
                rgb = plane(f.image, 2304, 1296, 1, np.uint8, 3)
                rgb_levels.update(np.unique(rgb[::160, ::180]).tolist())
                if "rgb" not in snapshot and streaming is not None and time.monotonic() - streaming >= min(1., seconds / 2):
                    snapshot["rgb"] = rgb.copy()
                result["rgb_frames"] += 1
                return
            require(f.coordinate == 1 and f.clock == 2 and f.metadata, "Missing radial-depth metadata")
            metadata = json.loads(f.metadata)
            require((metadata["source_generation"].encode() == f.generation and metadata["generation"] == f.epoch
                     and metadata["tof_sequence"] == f.sequence
                     and metadata["tof_timestamp_ns"] == f.stamp), "Frame header and depth metadata disagree")
            require((metadata["sensor_mode"], metadata["candidate_mode"], metadata["dtof_fov_correction"]) ==
                    (sensor, candidate, 6), "Board mode or F6 mismatch")
            release = metadata["calibration_release"]
            require(release and release == snapshot.setdefault("release", release), "Missing or changing calibration release")
            depth = plane(f.image, width, height, 2, "<f4")
            require(np.isfinite(depth).all() and (depth >= 0).all(), "Non-finite or negative radial depth")
            positive = depth > 0
            cloud = Frame(size=c.sizeof(Frame))
            check(sdk.GetFramePointCloud(c.byref(f), c.byref(cloud)), "GetFramePointCloud")
            require(cloud.stream == 3 and cloud.coordinate == 2 and metadata["point_cloud_format"] == "xyz-f32le-metres"
                    and all(getattr(cloud, key) == getattr(f, key)
                            for key in ("mode", "epoch", "generation", "device", "sequence", "stamp", "clock")),
                    "Board cloud and depth identities differ")
            points = plane(cloud.image, width, height, 4, "<f4", 3)
            result["board_clouds"] += 1
            require(np.isfinite(points).all() and (points[~positive] == 0).all() and (points[:, :, 2][positive] > 0).all(),
                    "Invalid XYZ point")
            residual = np.abs(np.linalg.norm(points.astype(np.float64), axis=2) - depth)
            require(np.all(residual <= 2e-6 * (1 + depth)), "XYZ norm and radial depth disagree")
            max_error = max(max_error, float(residual.max()))
            result["depth_frames"] += 1
            if not ex.snr.data or not ex.mask.data:
                return
            snr = plane(ex.snr, width, height, 3, np.uint8)
            mask = plane(ex.mask, width, height, 3, np.uint8)
            require((mask <= 1).all() and (mask[positive] == 1).all(), "Depth contradicts validity mask")
            valid_counts.append(int(positive.sum()))
            snr_min, snr_max = min(snr_min, int(snr.min())), max(snr_max, int(snr.max()))
            result["quality_frames"] += 1
            if ("depth" not in snapshot and positive.any() and streaming is not None
                    and time.monotonic() - streaming >= min(1., seconds / 2)):
                ply = output / "board.ply"
                check(sdk.SavePointCloudPly(c.byref(f), str(ply).encode("utf-8")), "SavePointCloudPly")
                header, vertices = ply.read_bytes().split(b"end_header\n", 1)
                require(header.startswith(b"ply\nformat binary_little_endian 1.0\n")
                        and f"element vertex {positive.sum()}\n".encode() in header
                        and f"comment sequence {f.sequence}\n".encode() in header
                        and vertices == points[positive].tobytes(), "Exported PLY differs from the live board cloud")
                result["ply_matches_board_bytes"] = True
                snapshot.update(depth=depth.copy(), snr=snr.copy(), mask=mask.copy(), points=points.copy(), metadata=metadata)
        except Exception as exc:
            errors.append(exc)

    session = c.c_void_p()
    options = Options(size=c.sizeof(Options), abi=3, mode=mode, target=1, policy=1,
                      runtime=str(runtime).encode(), store=str(registry).encode())
    status = Status(size=c.sizeof(Status))
    started = time.monotonic()
    check(sdk.ConnectWithOptions(c.byref(options), None, None, None, c.byref(session)), "ConnectWithOptions")
    try:
        check(sdk.SubscribeEx(session, frame_callback, None, None), "SubscribeEx")
        next_log = started
        while time.monotonic() - started < 240:
            sdk.dispatch(session)
            if errors:
                raise errors[0]
            check(sdk.GetSessionStatus(session, c.byref(status)), "GetSessionStatus")
            now = time.monotonic()
            if now >= next_log:
                print(f"{name} state={status.state} stage={status.stage} error={status.error} "
                      f"RGB={result['rgb_frames']} depth={result['depth_frames']}", flush=True)
                next_log = now + 5
            require(status.state != 5, f"Capture failed, category {status.error}, stage {status.stage}")
            if status.state == 2 and status.video and status.depth:
                if streaming is None:
                    streaming = now
                if now - streaming >= seconds and result["quality_frames"] >= 30 and result["rgb_frames"] >= 30:
                    break
            elif streaming is not None:
                raise RuntimeError("Capture left STREAMING during the observation")
        else:
            raise RuntimeError("Timed out waiting for both streams and quality frames")
        require(len(generations) == 1 and next(iter(generations))[0] == status.epoch, "Mixed capture generations")
        require(len(rgb_levels) > 1 and "depth" in snapshot, "Missing scene signal")
        result.update(streaming_observed_s=round(now - streaming, 2), connect_to_streaming_s=round(streaming - started, 2),
                      calibration_release=snapshot["release"], processing_fov=6, single_generation=True,
                      rgb_sample_levels=len(rgb_levels), valid_depth_pixels_min=min(valid_counts),
                      valid_depth_pixels_max=max(valid_counts), snr_min=snr_min, snr_max=snr_max,
                      sample_sequence=snapshot["metadata"]["tof_sequence"],
                      sample_tof_timestamp_ns=snapshot["metadata"]["tof_timestamp_ns"],
                      xyz_radial_max_abs_error_m=max_error)
        collecting = False
        check(sdk.Stop(session), "Stop")
        deadline = time.monotonic() + 120
        while time.monotonic() < deadline:
            sdk.dispatch(session)
            check(sdk.GetSessionStatus(session, c.byref(status)), "GetSessionStatus after Stop")
            if status.state == 0 and not status.busy:
                break
        else:
            raise RuntimeError("Stop did not finish")
        result["stopped"] = True
    except Exception:
        sdk.save_failure(session, output / "failure-state.private.json")
        raise
    finally:
        collecting = False
        while True:
            rc = sdk.Disconnect(session)
            if rc == 0:
                break
            require(rc == 2, f"Disconnect returned {rc}")
            sdk.dispatch(session)
    result["released"] = True
    write_json(output / "frame-metadata.private.json", snapshot["metadata"])
    Picture.fromarray(snapshot["rgb"]).save(output / "rgb.png")
    snapshot["depth"].tofile(output / f"radial-{width}x{height}.f32le")
    Picture.fromarray(np.rint(snapshot["depth"] * 1000).astype(np.uint16)).save(output / "depth-mm.png")
    Picture.fromarray(snapshot["snr"]).save(output / "snr.png")
    Picture.fromarray(snapshot["mask"]).save(output / "valid-0-1.png")
    points = snapshot["points"][snapshot["depth"] > 0]
    header = "ply\nformat ascii 1.0\nelement vertex %d\nproperty float x\nproperty float y\nproperty float z\nend_header" % len(points)
    np.savetxt(output / "points.ply", points, fmt="%.9g", header=header, comments="")
    write_json(output / "summary.json", result)
    print(name, json.dumps(result), flush=True)
    return result


def standard_runtime_hash(runtime):
    components = json.loads((runtime.parent / "runtime-components.json").read_text(encoding="utf-8"))
    entries = []
    for name in sorted(components["profiles"]["STANDARD"]["files"]):
        digest = hashlib.sha256((runtime / name).read_bytes()).hexdigest()
        entries.append(f"{name}\t{digest}\n")
    return hashlib.sha256("".join(entries).encode("utf-8")).hexdigest()


def verify_transitions(sdk, runtime, registry, output, modes):
    session = c.c_void_p()
    options = Options(size=c.sizeof(Options), abi=3, mode=1, target=1, policy=1,
                      runtime=str(runtime).encode(), store=str(registry).encode())
    status = Status(size=c.sizeof(Status))
    streams, epochs, generations, steps = {}, set(), set(), []

    @sdk.callback
    def observe(_, pointer):
        f = pointer.contents.frame
        if f.generation:
            streams[f.stream] = (f.mode, f.epoch, f.generation)

    def ready(name, mode):
        deadline = time.monotonic() + 120
        while time.monotonic() < deadline:
            sdk.dispatch(session)
            check(sdk.GetSessionStatus(session, c.byref(status)), name)
            require(status.state != 5, f"{name}: capture error {status.error}")
            frame = streams.get(1)
            if (status.state == 2 and not status.busy and frame and frame == streams.get(2)
                    and frame[:2] == (mode, status.epoch) and frame[1] not in epochs and frame[2] not in generations):
                epochs.add(frame[1])
                generations.add(frame[2])
                steps.append({"operation": name, "mode": mode, "epoch": frame[1]})
                print(name, "both streams, epoch", frame[1], flush=True)
                return
        raise RuntimeError(f"{name}: no fresh pair of video and depth generations")

    check(sdk.ConnectWithOptions(c.byref(options), None, None, None, c.byref(session)), "ConnectWithOptions")
    try:
        check(sdk.SubscribeEx(session, observe, None, None), "SubscribeEx")
        ready("connect", 1)
        for mode in modes[1:]:
            check(sdk.SetMode(session, mode), "SetMode")
            ready("switch", mode)
        check(sdk.Reconnect(session), "Reconnect")
        ready("reconnect", modes[-1])
        check(sdk.Stop(session), "Stop")
        deadline = time.monotonic() + 120
        while time.monotonic() < deadline:
            sdk.dispatch(session)
            check(sdk.GetSessionStatus(session, c.byref(status)), "Stop")
            if status.state == 0 and not status.busy:
                break
        else:
            raise RuntimeError("Stop before restart timed out")
        check(sdk.Start(session), "Start")
        ready("restart", modes[-1])
    except Exception:
        sdk.save_failure(session, output / "transitions-failure.private.json")
        raise
    finally:
        while (rc := sdk.Disconnect(session)) == 2:
            sdk.dispatch(session)
        check(rc, "Disconnect")
    return {"steps": steps, "distinct_generations": len(generations), "released": True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ("runtime", "registry", "output"):
        parser.add_argument("--" + arg, type=Path, required=True)
    parser.add_argument("--seconds", type=int, default=10)
    parser.add_argument("--transitions", action="store_true", help="Also switch, reconnect and restart on one handle")
    parser.add_argument("--development-runtime", action="store_true", help="Use an unpackaged local build without a package manifest")
    parser.add_argument("--all-modes", action="store_true", help="Capture both precision options at all three resolutions")
    args = parser.parse_args()
    require(os.environ.get("RV_TEST_OFFLINE_ONLY") != "1", "Live test cannot run with RV_TEST_OFFLINE_ONLY=1")
    require(args.seconds > 0 and args.registry.is_file(), "Provide a saved registry and a positive duration")
    runtime, registry, output = args.runtime.resolve(), args.registry.resolve(), args.output.resolve()
    sdk = SDK(runtime)
    require(sdk.AbiVersion() == 3 and c.sizeof(ExtendedFrame) == 176, "ABI mismatch")
    version = Version(size=c.sizeof(Version))
    check(sdk.GetVersionInfo(c.byref(version)), "GetVersionInfo")
    output.mkdir(parents=True, exist_ok=False)
    presets = MODES if args.all_modes else MODES[:2]
    dimensions = {"resolutions": sorted({(p[2], p[3]) for p in presets})} if args.all_modes else {"width": 48, "height": 32}
    report = {"sdk_version": version.version.decode(), "abi_version": version.abi,
              "tested_at_utc": datetime.now(timezone.utc).isoformat(),
              **({"windows_build": os.sys.getwindowsversion().build} if os.name == "nt" else
                 {"platform": platform.system(), "kernel": platform.release(), "architecture": platform.machine()}),
              ("dll_sha256" if os.name == "nt" else "library_sha256"): hashlib.sha256(sdk.path.read_bytes()).hexdigest(),
              **({"runtime_source": "development build"} if args.development_runtime else
                 {"standard_runtime_sha256": standard_runtime_hash(runtime)}),
              "rgb": {"width": 2304, "height": 1296, "format": "RGB8"},
              "depth": {**dimensions, "format": "F32_LE", "unit": "radial metres"},
              "quality": "same-frame U8 first-peak SNR and 0/1 valid_mask",
              "point_cloud": "board XYZ in the ToF frame, metres",
              "modes": [capture(sdk, runtime, registry, output / preset[1], preset, args.seconds) for preset in presets]}
    if args.transitions:
        report["transitions"] = verify_transitions(sdk, runtime, registry, output, [p[0] for p in presets])
    write_json(output / "summary.json", report)


if __name__ == "__main__":
    main()

