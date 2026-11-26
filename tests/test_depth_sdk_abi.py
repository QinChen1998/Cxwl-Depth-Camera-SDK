"""Exercise the Windows SDK from a process without Qt or device access."""

import argparse
from contextlib import nullcontext
import base64
import ctypes as c
import json
import math
import os
from pathlib import Path
import struct
import threading


class Options(c.Structure):
    _fields_ = [("size", c.c_uint32), ("abi_version", c.c_uint32),
                ("runtime_directory", c.c_char_p), ("device_store", c.c_char_p)]


class Image(c.Structure):
    _fields_ = [("data", c.c_void_p), ("byte_count", c.c_uint64),
                ("width", c.c_uint32), ("height", c.c_uint32),
                ("row_stride_bytes", c.c_uint32), ("pixel_format", c.c_uint32)]


class Point(c.Structure):
    _fields_ = [("x_m", c.c_float), ("y_m", c.c_float), ("z_m", c.c_float)]


class Frame(c.Structure):
    _fields_ = [(name, c.c_uint32) for name in ("size", "stream", "mode", "coordinate")] + [
        (name, c.c_uint64) for name in ("epoch", "sequence", "stamp")] + [
        ("clock", c.c_uint32), ("device", c.c_char_p), ("generation", c.c_char_p),
        ("image", Image), ("metadata", c.c_char_p)]


def write_registry(path):
    devices = []
    for number in (1, 2):
        key = struct.pack(">I", 11) + b"ssh-ed25519" + struct.pack(">I", 32) + bytes([number]) * 32
        serial = f"{number:016x}"
        devices.append({
            "id": f"fixture-{number}", "name": f"CXWL Depth Camera - {serial}" if number == 1 else "Offline device 2",
            "host": "127.0.0.1", "ssh_port": 22, "serial": serial,
            "public_key": base64.b64encode(key).decode("ascii"), "username": "root",
            "calibration": "rv20261109-f6", "input_rotation": -1, "camera_mount_rotation": 0,
            "legacy_coordinator": True, "last_connected": "",
        })
    path.write_text(json.dumps({
        "schema": 1, "kind": "cxwl-depth-camera-devices", "active": "fixture-1", "devices": devices,
    }), encoding="utf-8")


def run(dll_path, work):
    dll = c.CDLL(str(dll_path))
    assert not hasattr(dll, "DctsDepth_Connect")
    for retired in ("DetectInferenceDevices", "ListInferenceDevices",
                    "SelectInferenceDevice", "BenchmarkInferenceDevices"):
        assert not hasattr(dll, f"CxwlDepth_{retired}")

    handle = c.c_void_p

    def bind(name, *types):
        function = getattr(dll, "CxwlDepth_" + name)
        function.argtypes = types
        function.restype = c.c_int32
        return function

    abi = bind("AbiVersion")
    connect = bind("Connect", c.c_uint32, c.c_void_p, c.c_void_p, c.c_void_p, c.POINTER(handle))
    disconnect = bind("Disconnect", handle)
    open_sdk = bind("Open", c.POINTER(Options), c.POINTER(handle))
    close = bind("Close", handle)
    select = bind("SelectDevice", handle, c.c_char_p)
    mode = bind("SetMode", handle, c.c_uint32)
    poll = bind("Poll", handle)
    stop = bind("Stop", handle)
    check_usb = bind("CheckUsb", handle)
    list_devices = bind("ListDevices", handle, c.c_void_p, c.POINTER(c.c_uint32))
    get_state = bind("GetState", handle, c.c_void_p, c.POINTER(c.c_uint32))

    def read(call, session):
        required = c.c_uint32(0)
        assert call(session, None, c.byref(required)) == 0 and required.value > 0
        small = c.create_string_buffer(1)
        capacity = c.c_uint32(1)
        assert call(session, small, c.byref(capacity)) == 5 and capacity.value == required.value
        output = c.create_string_buffer(required.value)
        assert call(session, output, c.byref(required)) == 0
        return json.loads(output.value)

    registry = work / "devices.json"
    write_registry(registry)
    before = registry.read_bytes()
    options = Options(c.sizeof(Options), 3, str(dll_path.parent).encode("utf-8"), str(registry).encode("utf-8"))
    session = handle()
    assert abi() == 3
    assert connect(0, None, None, None, c.byref(session)) == 1 and not session.value
    assert connect(1, None, None, None, None) == 1
    for preset in range(1, 7):
        assert connect(preset, None, None, None, c.byref(session)) == 6 and not session.value
    assert disconnect(None) == 1
    for cycle in range(2):
        print(f"SDK open cycle {cycle + 1}", flush=True)
        assert open_sdk(c.byref(options), c.byref(session)) == 0 and session.value
        try:
            other = handle()
            assert open_sdk(c.byref(options), c.byref(other)) == 2 and not other.value
            wrong_thread = []
            thread = threading.Thread(target=lambda: wrong_thread.extend((poll(session), disconnect(session))))
            thread.start()
            thread.join()
            assert wrong_thread == [3, 3]
            devices = read(list_devices, session)
            assert [row["id"] for row in devices] == ["fixture-1", "fixture-2"]
            assert [row["selected"] for row in devices] == [True, False]
            assert [row["name"] for row in devices] == ["CXWL Depth Camera - 0000000000000001", "Offline device 2"]
            assert all(row["family"] == "CXWL Depth Camera" and row["modes"] == [1, 2] and row["ready"] for row in devices)
            assert mode(session, 0) == 1
            assert read(get_state, session)["mode"] == 1
            assert select(session, b"unknown-device") == 4
            assert select(session, b"fixture-2") == 0
            assert mode(session, 2) == 0
            state = read(get_state, session)
            assert state["device_id"] == "fixture-2" and state["mode"] == 2 and state["user_stopped"]
            assert mode(session, 1) == 0 and poll(session) == 0
            assert check_usb(session) == 6
            assert stop(session) == 0 and registry.read_bytes() == before
        finally:
            assert (close if cycle == 0 else disconnect)(session) == 0
        assert poll(session) == 1
    print("PASS SDK ABI, owner thread, one active session, device and mode selection, offline guard, and reopen")

    open_manager = bind("OpenDeviceManager", c.POINTER(Options), c.c_void_p, c.c_void_p, c.POINTER(handle))
    close_manager = bind("CloseDeviceManager", handle)
    poll_manager = bind("PollDeviceManager", handle)
    list_saved = bind("ListSavedDevices", handle, c.c_void_p, c.POINTER(c.c_uint32))
    list_saved_alloc = bind("ListSavedDevicesAlloc", handle, c.POINTER(handle), c.POINTER(c.c_uint32))
    free_json = bind("FreeJson", handle)
    manager_state = bind("GetDeviceManagerState", handle, c.c_void_p, c.POINTER(c.c_uint32))
    use_saved = bind("SetActiveDevice", handle, c.c_char_p)
    remove_saved = bind("RemoveSavedDevice", handle, c.c_char_p)
    target = bind("SetConnectionTarget", handle, c.c_uint32, c.c_uint64)
    scan = bind("ScanUsb", handle)
    maintain = bind("MaintainBoardFiles", handle, c.c_char_p, c.c_uint32)
    manager = handle()
    callbacks = []

    @c.CFUNCTYPE(None, c.c_void_p, c.c_uint32)
    def on_change(user, event):
        callbacks.append((user, event, manager_state(manager, None, c.byref(c.c_uint32())),
                          remove_saved(manager, b"unknown")))

    legacy = json.loads(before)
    legacy["kind"] = "rv1103-devices"
    legacy_bytes = json.dumps(legacy).encode("utf-8")
    registry.write_bytes(legacy_bytes)
    assert open_manager(c.byref(options), on_change, c.c_void_p(42), c.byref(manager)) == 0
    try:
        assert open_sdk(c.byref(options), c.byref(session)) == 2 and not session.value
        other = handle()
        assert open_manager(c.byref(options), None, None, c.byref(other)) == 2 and not other.value
        wrong_thread = []
        thread = threading.Thread(target=lambda: wrong_thread.extend((poll_manager(manager), close_manager(manager))))
        thread.start()
        thread.join()
        assert wrong_thread == [3, 3]
        devices = read(list_saved, manager)
        snapshot, size = handle(), c.c_uint32()
        assert list_saved_alloc(manager, c.byref(snapshot), c.byref(size)) == 0
        try:
            assert json.loads(c.string_at(snapshot, size.value - 1)) == devices
        finally:
            assert free_json(snapshot) == 0
        assert registry.read_bytes() == legacy_bytes
        assert [row["selected"] for row in devices] == [True, False]
        assert [row["name"] for row in devices] == ["", "Offline device 2"]
        assert all(set(row) == {"id", "name", "selected", "ready"} and row["ready"] for row in devices)
        assert len(devices[0]["id"]) == 64 and devices[0]["id"] != devices[1]["id"]
        assert "rgb_lsc" not in read(manager_state, manager)
        first, second = [row["id"].encode("ascii") for row in devices]
        assert registry.read_bytes() == legacy_bytes
        assert use_saved(manager, b"unknown") == 4
        assert use_saved(manager, second) == 0
        saved = json.loads(registry.read_bytes())
        assert saved["kind"] == "cxwl-depth-camera-devices" and saved["active"] == "fixture-2"
        assert [row["public_key"] for row in saved["devices"]] == [row["public_key"] for row in legacy["devices"]]
        assert [row["selected"] for row in read(list_saved, manager)] == [False, True]
        assert remove_saved(manager, first) == 0
        assert [row["id"].encode("ascii") for row in read(list_saved, manager)] == [second]
        assert callbacks == [(42, 7, 0, 2), (42, 7, 0, 2)]
        assert read(manager_state, manager)["can_manage"] is True
        assert target(manager, 3, 0) == 1 and target(manager, 1, 42) == 1
        assert target(manager, 2, 42) == 4 and target(manager, 1, 0) == 0
        assert scan(manager) == 6 and maintain(manager, second, 1) == 6
        assert poll_manager(manager) == 0
    finally:
        assert close_manager(manager) == 0
    assert poll_manager(manager) == 1
    assert open_manager(c.byref(options), None, None, c.byref(manager)) == 0
    try:
        assert read(list_saved, manager)[0]["id"].encode("ascii") == second
    finally:
        assert close_manager(manager) == 0
    print("PASS public device management, legacy registry, opaque IDs, callback and owner-thread rules, persistence")

    for removed in ("ProjectPointCloud", "GetCalibration", "StartRgbLscCalibration", "CheckRgbLsc"):
        assert not hasattr(dll, "CxwlDepth_" + removed)
    path = work / "点云 sample.ply"
    name = str(path).encode("utf-8")
    save = bind("SavePointCloudPly", c.POINTER(Frame), c.c_char_p)
    depth = (c.c_float * 64)(*([1.0] * 64))
    frame = Frame(size=c.sizeof(Frame), stream=2, coordinate=1, mode=3,
                  image=Image(c.addressof(depth), c.sizeof(depth), 8, 8, 32, 2))
    assert save(c.byref(frame), name) == 1 and not path.exists()
    getter = bind("GetFramePointCloud", c.POINTER(Frame), c.POINTER(Frame))
    output = Frame(size=c.sizeof(Frame))
    assert getter(c.byref(frame), c.byref(output)) == 6
    assert getter(None, c.byref(output)) == 1
    output.size = 1
    assert getter(c.byref(frame), c.byref(output)) == 5
    xyz = c.create_string_buffer(7 * 104 + 96)
    struct.pack_into("<fff", xyz, 104 + 12, 0.2, -0.5, 2.3)
    frame.stream, frame.coordinate = 3, 2
    frame.image = Image(c.addressof(xyz), c.sizeof(xyz), 8, 8, 104, 4)
    assert save(c.byref(frame), name) == 0
    data = path.read_bytes()
    assert data.split(b"end_header\n")[1] == xyz.raw[116:128]
    frame.image.byte_count -= 1
    assert save(c.byref(frame), name) == 1 and path.read_bytes() == data
    frame.image.byte_count += 1
    struct.pack_into("<f", xyz, 116, float("nan"))
    assert save(c.byref(frame), name) == 1 and path.read_bytes() == data
    for mode, width, height in ((3, 8, 8), (5, 16, 16), (1, 48, 32)):
        zeros = (Point * (width * height))()
        frame.mode = mode
        frame.image = Image(c.addressof(zeros), c.sizeof(zeros), width, height, width * 12, 4)
        assert save(c.byref(frame), name) == 0
        header, binary = path.read_bytes().split(b"end_header\n")
        assert b"element vertex 0\n" in header and not binary
    assert save(c.byref(frame), b"") == 1
    assert save(c.byref(frame), str(work / "missing" / "cloud.ply").encode()) == 6
    print("PASS board XYZ view scope, caller-owned padded XYZ and exact PLY values")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dll", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--qt-bin", type=Path)
    args = parser.parse_args()
    os.environ["RV_TEST_OFFLINE_ONLY"] = "1"
    args.output.mkdir(parents=True, exist_ok=False)
    dll = args.dll.resolve()
    with (os.add_dll_directory(str((args.qt_bin or dll.parent).resolve())) if os.name == "nt" else nullcontext()):
        run(dll, args.output.resolve())
        from test_depth_sdk_extensions import run_extensions
        run_extensions(dll, args.output.resolve())
        from test_depth_sdk_allocated import run_allocated
        run_allocated(dll, args.output.resolve())
