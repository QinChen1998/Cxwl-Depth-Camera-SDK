"""Owned JSON snapshots: real DLL, lifetime, invalid addresses and owner-thread validation."""
import ctypes as c
import json
import threading
from test_depth_sdk_abi import Options, write_registry

def run_allocated(dll_path, work):
    dll=c.CDLL(str(dll_path)); handle=c.c_void_p
    def bind(name,*types):
        f=getattr(dll,'CxwlDepth_'+name); f.argtypes=types; f.restype=c.c_int32; return f
    release=bind('FreeJson',c.c_void_p)
    registry=work/'allocated-fixture.json'; write_registry(registry)
    options=Options(c.sizeof(Options),3,str(dll_path.parent).encode(),str(registry).encode())
    retained=[]
    for manager,names in ((False,('ListDevices','GetState','GetUsbStatus')),(True,('ListSavedDevices','GetDeviceManagerState'))):
        if manager:
            open_fn=bind('OpenDeviceManager',c.POINTER(Options),handle,handle,c.POINTER(handle))
            close=bind('CloseDeviceManager',handle)
        else:
            open_fn=bind('Open',c.POINTER(Options),c.POINTER(handle));close=bind('Close',handle)
        h=handle()
        options.abi_version=2
        assert (open_fn(c.byref(options),None,None,c.byref(h)) if manager else open_fn(c.byref(options),c.byref(h)))==1 and not h.value
        options.abi_version=3
        assert (open_fn(c.byref(options),None,None,c.byref(h)) if manager else open_fn(c.byref(options),c.byref(h)))==0
        for name in names:
            query=bind(name+'Alloc',handle,c.POINTER(handle),c.POINTER(c.c_uint32))
            p=handle();count=c.c_uint32()
            assert query(h,c.byref(p),c.byref(count))==0 and p.value and count.value>1
            data=c.string_at(p,count.value); assert data[-1:]==b'\0'; json.loads(data[:-1])
            retained.append((p.value,data))
            other=handle(1);n=c.c_uint32(99)
            assert query(None,c.byref(other),c.byref(n))==1 and not other.value and n.value==0
            assert query(h,None,c.byref(n))==1 and n.value==0
            result=[]
            def wrong_thread():
                result.append(query(h,c.byref(other),c.byref(n)))
            t=threading.Thread(target=wrong_thread);t.start();t.join()
            assert result==[3] and not other.value and n.value==0
            for _ in range(50):
                assert query(h,c.byref(other),c.byref(n))==0
                assert release(other)==0
        assert close(h)==0
    # Snapshots survive further queries and closing both owning kinds of handles.
    for address,data in retained: assert c.string_at(address,len(data))==data
    results=[]
    def cross_thread_release():
        for address,_ in retained: results.append(release(address))
    t=threading.Thread(target=cross_thread_release);t.start();t.join()
    assert results==[0]*5
    assert release(retained[-1][0])==1
    foreign=c.create_string_buffer(b'not allocated by SDK')
    assert release(c.addressof(foreign))==1 and release(None)==0
    print('PASS ABI v3 allocated JSON: all five snapshots, invalid ABI/arguments, owner thread, post-close lifetime, cross-thread release and double-free rejection')
