"""Additive ABI tests against a real DLL, without connecting to hardware."""
import ctypes as c
import json
from pathlib import Path
import threading
import time
from test_depth_sdk_abi import Options, Point, write_registry


class ConnectOptions(c.Structure):
    _fields_ = [(n, c.c_uint32) for n in ('size', 'abi', 'mode', 'target')] + [
        ('check', c.c_uint64), ('policy', c.c_uint32),
        ('runtime', c.c_char_p), ('store', c.c_char_p), ('device', c.c_char_p)]


class Version(c.Structure):
    _fields_ = [(n, c.c_uint32) for n in ('size', 'abi', 'major', 'minor', 'patch')] + [
        ('version', c.c_char * 32), ('build', c.c_char * 64)]


class Error(c.Structure):
    _fields_ = [('size', c.c_uint32), ('code', c.c_int32), ('function', c.c_char * 96), ('message', c.c_char * 512)]


class Status(c.Structure):
    _fields_ = [(n, c.c_uint32) for n in ('size', 'state', 'error', 'mode', 'stage', 'busy', 'video', 'depth',
                                        'retaining', 'switching', 'estimate', 'files')] + [
        ('epoch', c.c_uint64), ('fps', c.c_double), ('device', c.c_char * 128), ('repair', c.c_char * 128)]


class Device(c.Structure):
    _fields_ = [('size', c.c_uint32), ('ready', c.c_uint32), ('device', c.c_char * 128),
                ('name', c.c_char * 256), ('serial', c.c_char * 128), ('calibration', c.c_char * 96)]


class Capabilities(c.Structure):
    _fields_ = [('size', c.c_uint32), ('count', c.c_uint32), ('modes', c.c_uint32 * 6)] + [
        (n, c.c_uint32) for n in ('dw', 'dh', 'df', 'vw', 'vh', 'vf', 'snr', 'valid', 'sync', 'sessions')]


def run_extensions(dll_path, work):
    dll = c.CDLL(str(dll_path))
    handle = c.c_void_p

    def bind(name, *args):
        f = getattr(dll, 'CxwlDepth_' + name)
        f.argtypes, f.restype = args, c.c_int32
        return f

    version = bind('GetVersionInfo', c.POINTER(Version))
    error = bind('GetLastError', c.POINTER(Error))
    caps = bind('GetCapabilities', c.POINTER(Capabilities))
    state = bind('GetSessionStatus', handle, c.POINTER(Status))
    device = bind('GetDeviceInfo', handle, c.POINTER(Device))
    connect = bind('ConnectWithOptions', c.POINTER(ConnectOptions), handle, handle, handle, c.POINTER(handle))
    open_sdk = bind('Open', c.POINTER(Options), c.POINTER(handle))
    close = bind('Close', handle)
    stop = bind('Stop', handle)
    wait = bind('WaitForEvent', handle, c.c_uint32, c.POINTER(c.c_uint32))
    subscribe = bind('SubscribeEx', handle, handle, handle, handle)
    logger = bind('SetLogCallback', c.c_uint32, handle, handle)
    logs = []

    @c.CFUNCTYPE(None, handle, c.c_uint32, c.c_char_p)
    def log(user, severity, message):
        # Logger reentrancy must return BUSY instead of deadlocking.
        logs.append((user, severity, message, version(c.byref(Version(size=c.sizeof(Version))))))

    assert logger(3, log, handle(17)) == 0
    v = Version(size=c.sizeof(Version))
    product = json.loads((Path(__file__).resolve().parents[1] / 'sdk/product.json').read_text(encoding='utf-8'))
    assert version(c.byref(v)) == 0 and v.abi == product['abi_version']
    assert (v.major, v.minor, v.patch) == tuple(map(int, product['sdk_version'].split('.')))
    assert v.version.decode() == product['sdk_version'] and v.build
    v.size = 4
    assert version(c.byref(v)) == 5
    e = Error(size=c.sizeof(Error))
    assert error(c.byref(e)) == 0 and e.code == 5 and e.function == b'CxwlDepth_GetVersionInfo' and e.message
    assert logs[-1][0:2] == (17, 1) and logs[-1][3] == 2
    cp = Capabilities(size=c.sizeof(Capabilities))
    assert caps(c.byref(cp)) == 0
    assert cp.count == 6 and list(cp.modes) == list(range(1, 7)) and (cp.dw, cp.dh, cp.snr, cp.valid, cp.sync, cp.sessions) == (48, 32, 1, 1, 0, 1)
    assert error(c.byref(e)) == 0 and e.code == 5  # Success did not erase failure.
    co = ConnectOptions(size=c.sizeof(ConnectOptions), abi=3, mode=1)
    session = handle(123)
    co.target, co.policy = 2, 1
    assert connect(c.byref(co), None, None, None, c.byref(session)) == 1 and not session.value
    co.target, co.policy = 0, 2  # Retired calibration-offer policy.
    assert connect(c.byref(co), None, None, None, c.byref(session)) == 1 and not session.value
    co.target, co.policy = 0, 0
    assert connect(c.byref(co), None, None, None, c.byref(session)) == 6 and not session.value
    assert open_sdk(None, c.byref(session)) == 1
    assert error(c.byref(e)) == 0 and e.function == b'CxwlDepth_Open'
    registry = work / 'extensions-devices.json'
    write_registry(registry)
    paths = Options(c.sizeof(Options), 3, str(dll_path.parent).encode(), str(registry).encode())
    assert open_sdk(c.byref(paths), c.byref(session)) == 0
    try:
        st = Status(size=c.sizeof(Status))
        assert state(session, c.byref(st)) == 0 and st.state == 0 and st.device == b'fixture-1'
        assert logger(3, log, handle(19)) == 0
        assert version(None) == 1  # Logger can be updated during a live handle.
        assert logs[-1][0] == 19
        di = Device(size=c.sizeof(Device))
        assert device(session, c.byref(di)) == 0 and di.ready and not di.calibration
        assert subscribe(session, None, None, None) == 0
        wrong = []
        def thread_test():
            local = Error(size=c.sizeof(Error))
            wrong.append(state(session, c.byref(Status(size=c.sizeof(Status)))))
            assert logger(3, None, None) == 3
            assert error(c.byref(local)) == 0 and local.code == 3
        thread = threading.Thread(target=thread_test)
        thread.start(); thread.join()
        assert wrong == [3]
        assert error(c.byref(e)) == 0 and e.code == 1  # Main thread's error is unchanged.
        dispatched = c.c_uint32(99)
        assert wait(session, 0, c.byref(dispatched)) == 0 and dispatched.value == 0
        # GetTickCount64 can quantize a 25 ms Windows wait to 16 ms. Use the performance counter.
        start = time.perf_counter()
        assert wait(session, 25, c.byref(dispatched)) == 0 and dispatched.value == 0
        assert time.perf_counter() - start >= 0.020
        assert error(c.byref(e)) == 0 and e.function == b'CxwlDepth_GetVersionInfo'
        assert wait(session, 1, None) == 1
        dispatched.value = 99
        assert wait(session, 0xffffffff, c.byref(dispatched)) == 1 and dispatched.value == 0
        wrong = []
        thread = threading.Thread(target=lambda: wrong.append(wait(session, 0, c.byref(dispatched))))
        thread.start(); thread.join()
        assert wrong == [3] and dispatched.value == 0
        assert stop(session) == 0 and state(session, c.byref(st)) == 0 and st.state == 0
    finally:
        assert close(session) == 0
    assert logger(3, None, None) == 0
    recover = bind('RecoverDevice', handle, c.c_char_p)
    manager_open = bind('OpenDeviceManager', c.POINTER(Options), handle, handle, c.POINTER(handle))
    manager_close = bind('CloseDeviceManager', handle)
    manager = handle()
    assert recover(None, b'device') == 1 and recover(None, None) == 1
    assert manager_open(c.byref(paths), None, None, c.byref(manager)) == 0
    registry_before_recovery = registry.read_bytes()
    try:
        assert recover(manager, None) == 1 and recover(manager, b'') == 1
        assert recover(manager, b'offline-device') == 6
        wrong = []
        thread = threading.Thread(target=lambda: wrong.append(recover(manager, b'offline-device')))
        thread.start(); thread.join()
        assert wrong == [3]
        assert registry.read_bytes() == registry_before_recovery
    finally:
        assert manager_close(manager) == 0
    print('PASS additive SDK version, typed status, six-mode capabilities, device calibration receipt, errors, logger, wait and offline options')
