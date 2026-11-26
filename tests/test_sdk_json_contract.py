"""Validate JSON Schema files, examples and real offline DLL JSON responses."""
import argparse
from contextlib import nullcontext
import ctypes as c
import json
import os
from pathlib import Path
from test_depth_sdk_abi import Options, write_registry

def run(dll_path, schemas, output, selftests=()):
    from jsonschema import Draft202012Validator
    definitions = {p.name.removesuffix('.schema.json'): json.loads(p.read_text(encoding='utf-8'))
                   for p in schemas.glob('*.schema.json')}
    for schema in definitions.values():
        Draft202012Validator.check_schema(schema)
    validators = {name: Draft202012Validator(schema) for name, schema in definitions.items()}
    for report in selftests:
        data = json.loads(report.read_text(encoding='utf-8'))
        validators['SelfTestReport'].validate(data)
        assert not validators['SelfTestReport'].is_valid({**data, 'schema': 0})
    samples = json.loads((schemas / 'samples.json').read_text(encoding='utf-8'))
    for name, sample in samples.items():
        validators[name.split('_')[0]].validate(sample)
    assert not validators['GetState'].is_valid({**samples['GetState_Connect'], 'mode': 24})
    assert not validators['GetUsbStatus'].is_valid({'state': 'idle'})
    assert not validators['FrameMetadata'].is_valid({**samples['FrameMetadata'], 'width': 640})
    for value in (2**53 - 1, 2**53, 2**53 + 1):
        metadata = {**samples['FrameMetadata'], 'tof_timestamp_ns': value, 'raw_host_dispatch_ns': str(value)}
        validators['FrameMetadata'].validate(metadata)
        assert not validators['FrameMetadata'].is_valid({**metadata, 'raw_host_dispatch_ns': value})
        assert not validators['FrameMetadata'].is_valid({**metadata, 'tof_timestamp_ns': str(value)})
        usb = {**samples['GetUsbStatus'], 'check_id': value}
        validators['GetUsbStatus'].validate(usb)
        assert not validators['GetUsbStatus'].is_valid({**usb, 'check_id': str(value)})
    manager_sample = samples['GetDeviceManagerState']
    assert not validators['GetDeviceManagerState'].is_valid({**manager_sample, 'usb': {**manager_sample['usb'], 'device_id': 'example'}})
    assert "rgb_lsc" not in definitions["GetDeviceManagerState"]["properties"]
    dll = c.CDLL(str(dll_path))
    def bind(name, *args):
        f=getattr(dll,'CxwlDepth_'+name); f.argtypes=args; f.restype=c.c_int32; return f
    handle=c.c_void_p
    open_sdk=bind('Open',c.POINTER(Options),c.POINTER(handle))
    close=bind('Close',handle)
    manager_open=bind('OpenDeviceManager',c.POINTER(Options),handle,handle,c.POINTER(handle))
    manager_close=bind('CloseDeviceManager',handle)
    functions={n:bind(n,handle,handle,c.POINTER(c.c_uint32)) for n in
               ('ListDevices','GetState','GetUsbStatus','ListSavedDevices','GetDeviceManagerState')}
    def read(name, h):
        size=c.c_uint32()
        assert functions[name](h,None,c.byref(size))==0 and size.value>0
        for _ in range(8):
            buffer=c.create_string_buffer(size.value)
            result=functions[name](h,buffer,c.byref(size))
            if result==5: continue
            assert result==0
            value=json.loads(buffer.value.decode('utf-8'))
            validators[name].validate(value)
            return value
        raise AssertionError('JSON grew repeatedly')
    registry=output/'devices.json'; write_registry(registry)
    options=Options(c.sizeof(Options),3,str(dll_path.parent).encode(),str(registry).encode())
    session=handle()
    assert open_sdk(c.byref(options),c.byref(session))==0
    observed={}
    try:
        for name in ('ListDevices','GetState','GetUsbStatus'): observed[name]=read(name,session)
    finally: assert close(session)==0
    manager=handle()
    assert manager_open(c.byref(options),None,None,c.byref(manager))==0
    try:
        for name in ('ListSavedDevices','GetDeviceManagerState'): observed[name]=read(name,manager)
    finally: assert manager_close(manager)==0
    # These contain only synthetic fixture identities; never query live registries.
    (output/'observed-json.json').write_text(json.dumps(observed,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(f'PASS {len(validators)} schemas, {len(selftests)} SelfTest reports, samples, 64-bit boundaries and five live library queries offline')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--dll',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--schemas',type=Path,default=Path(__file__).resolve().parents[1]/'docs/sdk/schemas')
    p.add_argument('--selftest-report', action='append', type=Path, default=[])
    args=p.parse_args()
    os.environ['RV_TEST_OFFLINE_ONLY']='1'
    args.output.mkdir(parents=True,exist_ok=False)
    with (os.add_dll_directory(str(args.dll.resolve().parent)) if os.name == "nt" else nullcontext()):
        run(args.dll.resolve(),args.schemas,args.output.resolve(),args.selftest_report)

