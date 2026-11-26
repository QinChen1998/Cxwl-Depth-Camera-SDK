"""Measure live 48x32 throughput and bounded producer-to-callback age (not optical latency)."""
import argparse
import ctypes as c
import json
import math
import os
from pathlib import Path
import statistics
import time

class Image(c.Structure):
    _fields_=[('data',c.c_void_p),('bytes',c.c_uint64)]+[(n,c.c_uint32) for n in ('width','height','stride','format')]
class Frame(c.Structure):
    _fields_=[(n,c.c_uint32) for n in ('size','stream','mode','coordinate')]+[(n,c.c_uint64) for n in ('epoch','sequence','stamp')]+[
        ('clock',c.c_uint32),('device',c.c_char_p),('generation',c.c_char_p),('image',Image),('metadata',c.c_char_p)]
class Options(c.Structure):
    _fields_=[(n,c.c_uint32) for n in ('size','abi','mode','target')]+[('check',c.c_uint64),('policy',c.c_uint32),
        ('runtime',c.c_char_p),('store',c.c_char_p),('device',c.c_char_p)]
class Paths(c.Structure):
    _fields_=[('size',c.c_uint32),('abi',c.c_uint32),('runtime',c.c_char_p),('store',c.c_char_p)]

def host_dispatch_clock():
    if os.name != 'nt':
        return time.perf_counter_ns

    # MSVC steady_clock uses the raw performance counter. Older CPython versions
    # subtract a process-local origin from that counter in perf_counter_ns().
    kernel = c.WinDLL('kernel32', use_last_error=True)
    frequency = c.c_int64()
    counter = c.c_int64()
    for function in (kernel.QueryPerformanceFrequency, kernel.QueryPerformanceCounter):
        function.argtypes = (c.POINTER(c.c_int64),)
        function.restype = c.c_int32
    if not kernel.QueryPerformanceFrequency(c.byref(frequency)) or frequency.value <= 0:
        raise RuntimeError('Cannot read Windows performance counter frequency')

    def now_ns():
        if not kernel.QueryPerformanceCounter(c.byref(counter)):
            raise c.WinError(c.get_last_error())
        return counter.value * 1_000_000_000 // frequency.value

    return now_ns


def distribution(values):
    ordered=sorted(values)
    def percentile(p):
        index=(len(ordered)-1)*p; low=int(index); high=min(low+1,len(ordered)-1)
        return ordered[low]+(ordered[high]-ordered[low])*(index-low)
    return {'min':ordered[0],'p50':percentile(.5),'p95':percentile(.95),'p99':percentile(.99),
            'max':ordered[-1],'mean':statistics.mean(ordered)}

def measure(args):
    dll=c.CDLL(str(args.dll.resolve())); handle=c.c_void_p
    def bind(name,*types):
        f=getattr(dll,'CxwlDepth_'+name); f.argtypes=types; f.restype=c.c_int32; return f
    assert bind('AbiVersion')()==2 and c.sizeof(Frame)==104
    connect=bind('ConnectWithOptions',c.POINTER(Options),c.c_void_p,c.c_void_p,c.c_void_p,c.POINTER(handle))
    poll=bind('Poll',handle); disconnect=bind('Disconnect',handle)
    get_state=bind('GetStateAlloc',handle,c.POINTER(c.c_void_p),c.POINTER(c.c_uint32))
    free=bind('FreeJson',c.c_void_p)
    manager=handle()
    def close_manager():
        if not manager.value: return
        close=bind('CloseDeviceManager',handle); pump=bind('PollDeviceManager',handle)
        while True:
            r=close(manager)
            if r==0: manager.value=None; return
            if r!=2 or pump(manager): raise RuntimeError('Manager shutdown failed')
            time.sleep(.01)
    records=[]; errors=[]; first=None; video_count=0; sample_meta={}; identities=set()
    host_now_ns = host_dispatch_clock()
    callback_type=c.CFUNCTYPE(None,c.c_void_p,c.POINTER(Frame))
    @callback_type
    def frame_callback(_, pointer):
        nonlocal first,video_count,sample_meta
        try:
            host_ns=host_now_ns()
            f=pointer.contents
            if f.stream==1: video_count+=1; return
            if f.size!=c.sizeof(Frame) or f.image.width!=48 or f.image.height!=32: raise ValueError('Unexpected frame layout')
            meta=json.loads(f.metadata)
            if first is None: first=time.monotonic()
            if time.monotonic()-first<args.warmup: return
            dispatch_ns=int(meta['raw_host_dispatch_ns'])
            dispatch_ms=(host_ns-dispatch_ns)/1e6
            if not 0<=dispatch_ms<5000: raise ValueError('Host clock basis mismatch')
            lower=meta['raw_board_age_ms']+dispatch_ms
            upper=lower+meta['raw_request_roundtrip_ms']
            valid=0
            for row in range(32):
                pixels=(c.c_float*48).from_address(f.image.data+row*f.image.stride)
                valid+=sum(math.isfinite(v) and v>0 for v in pixels)
            records.append({'sequence':f.sequence,'board_ns':f.stamp,'host_ns':host_ns,'lower_ms':lower,'upper_ms':upper,
                            'request_ms':meta['raw_request_roundtrip_ms'],'host_dispatch_ms':dispatch_ms,'valid_pixels':valid})
            identities.add((f.epoch,f.generation))
            if not sample_meta:
                sample_meta={k:v for k,v in meta.items() if any(token in k for token in ('settings','integration','period','iterations','snr','sensor_mode','candidate_mode','calibration_release','fov'))}
        except Exception as error: errors.append(str(error))
    options=Options(size=c.sizeof(Options),abi=3,mode=args.mode,target=1,policy=1,
                    runtime=str(args.runtime.resolve()).encode(),store=str(args.store.resolve()).encode())
    if args.usb_enroll:
        paths=Paths(c.sizeof(Paths),2,options.runtime,options.store)
        open_manager=bind('OpenDeviceManager',c.POINTER(Paths),handle,handle,c.POINTER(handle))
        scan=bind('ScanUsb',handle); pump=bind('PollDeviceManager',handle)
        manager_state=bind('GetDeviceManagerStateAlloc',handle,c.POINTER(handle),c.POINTER(c.c_uint32))
        if open_manager(c.byref(paths),None,None,c.byref(manager)): raise RuntimeError('OpenDeviceManager failed')
        try:
            if scan(manager): raise RuntimeError('ScanUsb failed')
            scan_deadline=time.monotonic()+180
            while time.monotonic()<scan_deadline:
                if pump(manager): raise RuntimeError('PollDeviceManager failed')
                raw=handle(); count=c.c_uint32()
                if manager_state(manager,c.byref(raw),c.byref(count)): raise RuntimeError('Manager state failed')
                try: state=json.loads(c.string_at(raw,count.value-1))
                finally: free(raw)
                usb=state.get('usb',{})
                if not state.get('busy') and usb.get('check_id'):
                    options.target=2;options.policy=0;options.check=int(usb['check_id']);options.runtime=None;options.store=None
                    print('USB identity checked; enrolling in isolated measurement registry',flush=True);break
                if usb.get('state')=='error': raise RuntimeError('USB check failed: '+str(usb.get('error')))
                time.sleep(.01)
            else: raise RuntimeError('USB scan timed out')
        except BaseException:
            close_manager(); raise
    session=handle(); result=connect(c.byref(options),frame_callback,None,None,c.byref(session))
    if result:
        close_manager(); raise RuntimeError(f'ConnectWithOptions failed: {result}')
    start=time.monotonic(); last_status=0; failed=None
    try:
        while time.monotonic()-start<args.timeout:
            if poll(session): raise RuntimeError('Poll failed')
            now=time.monotonic()
            if now-last_status>5:
                p=c.c_void_p(); size=c.c_uint32()
                if get_state(session,c.byref(p),c.byref(size)): raise RuntimeError('GetState failed')
                try: state=json.loads(c.string_at(p,size.value-1))
                finally: free(p)
                print(json.dumps({'elapsed_s':round(now-start,1),'depth_samples':len(records),'video_frames':video_count,
                                  'connection':state.get('connection_state'),'error':state.get('error')}),flush=True)
                if state.get('connection_state')=='error':
                    (args.output/'failure-state.private.json').write_text(json.dumps(state,indent=2,ensure_ascii=False),encoding='utf-8')
                    raise RuntimeError('Capture error; local diagnostic saved')
                last_status=now
            if errors: raise RuntimeError(errors[0])
            if first and now-first>=args.warmup+args.seconds: break
            time.sleep(.001)
    except Exception as error: failed=str(error)
    finally:
        close_start=time.monotonic()
        while True:
            result=disconnect(session)
            if result==0: break
            if result!=2: raise RuntimeError(f'Disconnect failed: {result}')
            if poll(session): raise RuntimeError('Poll during shutdown failed')
            if time.monotonic()-close_start>60: print('Still waiting for owned capture shutdown',flush=True); close_start=time.monotonic()
            time.sleep(.01)
        close_manager()
    (args.output/'frames.json').write_text(json.dumps(records,indent=2),encoding='utf-8')
    if failed: raise RuntimeError(failed)
    if len(records)<30: raise RuntimeError('Insufficient depth frames')
    if len(identities)!=1: raise RuntimeError('Capture generation changed; reject mixed-run timing')
    spans=[(b['board_ns']-a['board_ns'])/1e6 for a,b in zip(records,records[1:])]
    steps=[b['sequence']-a['sequence'] for a,b in zip(records,records[1:])]
    if min(spans)<=0 or min(steps)<=0: raise RuntimeError('Nonmonotonic capture identity')
    report={'sdk_abi':2,'mode':args.mode,'warmup_s':args.warmup,'requested_sample_s':args.seconds,
            'depth_samples':len(records),'video_frames_including_warmup':video_count,'generations':len(identities),
            'board_producer_fps':sum(steps)/((records[-1]['board_ns']-records[0]['board_ns'])/1e9),
            'host_callback_fps':(len(records)-1)/((records[-1]['host_ns']-records[0]['host_ns'])/1e9),
            'skipped_sequences':sum(v-1 for v in steps),'board_frame_interval_ms':distribution(spans),
            'producer_to_callback_lower_ms':distribution([r['lower_ms'] for r in records]),
            'producer_to_callback_upper_ms':distribution([r['upper_ms'] for r in records]),
            'request_roundtrip_ms':distribution([r['request_ms'] for r in records]),
            'valid_pixels':distribution([r['valid_pixels'] for r in records]),'capture_parameters':sample_meta,
            'clock':time.get_clock_info('perf_counter').implementation,'shutdown_complete':True,
            'scope':'Driver complete-frame publication to SDK callback; bounded by full request RTT, not optical end-to-end latency.'}
    (args.output/'summary.json').write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False),flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('dll','runtime','store','output'): p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--qt-bin',type=Path)
    p.add_argument('--usb-enroll',action='store_true',help='Explicitly authorize validated USB enrollment/preparation into the given registry')
    p.add_argument('--mode',type=int,choices=(1,2),default=2)
    p.add_argument('--seconds',type=float,default=60)
    p.add_argument('--warmup',type=float,default=5)
    p.add_argument('--timeout',type=float,default=240)
    args=p.parse_args(); args.output.mkdir(parents=True,exist_ok=False)
    if os.environ.get('RV_TEST_OFFLINE_ONLY')=='1': raise SystemExit('Live measurement refuses offline test environment')
    paths=[os.add_dll_directory(str(args.runtime.resolve()))]
    if args.qt_bin: paths.append(os.add_dll_directory(str(args.qt_bin.resolve())))
    # Python is outside the runtime directory. Preload the same app-local DLLs
    # using Windows' explicit DLL-directory search, as a native deployed app does.
    codecs=[c.CDLL(str(args.runtime.resolve()/name)) for name in
            ('avutil-59.dll','swresample-5.dll','avcodec-61.dll','avformat-61.dll','swscale-8.dll')]
    try: measure(args)
    finally:
        for path in paths: path.close()
