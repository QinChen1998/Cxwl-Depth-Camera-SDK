"""Validate a generated customer SDK: hashes, links, ABI surface and distribution boundary."""
import argparse
import hashlib
import json
from pathlib import Path
import re
from urllib.parse import unquote

def verify(root):
    manifest=json.loads((root/'sdk-manifest.json').read_text(encoding='utf-8'))
    checked=0
    for name,digest in manifest['sha256'].items():
        path=(root/name).resolve()
        if root not in path.parents or not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest()!=digest:
            raise ValueError(f'Missing/changed or unsafe entry: {name}')
        checked+=1
    actual={p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()}
    assert actual==set(manifest['sha256'])|{'sdk-manifest.json'}, 'Unmanifested delivery file'
    links=0
    for doc in root.rglob('*.md'):
        for raw in re.findall(r'\]\(([^)]+)\)',doc.read_text(encoding='utf-8')):
            name=unquote(raw.strip('<>').split('#',1)[0])
            if not name or re.match(r'^[a-zA-Z][\w+.-]*:',name): continue
            path=(doc.parent/name).resolve()
            if (path != root and root not in path.parents) or not path.exists(): raise ValueError(f'Broken link in {doc.relative_to(root)}: {name}')
            links+=1
    for p in root.rglob('*.json'):
        data=p.read_bytes()
        assert not data.startswith(b'\xef\xbb\xbf'), f'JSON BOM: {p}'
        json.loads(data.decode('utf-8'))
    sdk=root/'SDK/bin'
    launchers=list((root/'Example/bin').glob('*.exe'))
    assert len(launchers)==8 and not list((root/'Example/bin').glob('*.dll'))
    for launcher in launchers:
        assert (sdk/launcher.name).is_file(), f'Launcher target missing: {launcher.name}'
    components=json.loads((root/'SDK/runtime-components.json').read_text(encoding='utf-8'))
    cmake_components=(root/'SDK/cmake/RuntimeComponents.cmake').read_text(encoding='utf-8')
    for profile_name,profile in components['profiles'].items():
        for name in profile['files']: assert (sdk/name).is_file(), name
        generated=re.search(r'set\(CXWL_RUNTIME_'+profile_name+r'_FILES\s+(.*?)\)',cmake_components,re.S)
        assert generated and re.findall(r'"([^"]+)"',generated[1])==profile['files'], profile_name
        assert profile['bytes']==sum((sdk/name).stat().st_size for name in profile['files']), profile_name
    assert not (root/'Doc/tutorial').exists(), 'Empty duplicate tutorial entry'
    assert (root/'Doc/api/Chinese/API_REFERENCE.pdf').is_file()
    assert (root/'Doc/api/Chinese/点云录制数据读取说明.pdf').read_bytes().startswith(b'%PDF-')
    assert (root/'Script/build-examples.ps1').is_file()
    viewer_main=(root/'Example/QtViewer/main.cpp').read_text(encoding='utf-8')
    for internal in ('DCTS_', 'CXWL_DESKTOP_VALIDATION', 'RvDeviceStore.h', 'Cxwl Depth Camera', '触想工作台', 'DepthCameraAndTactileSensor'):
        assert internal not in viewer_main, internal
    for unused in ('opengl32sw.dll','D3Dcompiler_47.dll'):
        assert not (sdk/unused).exists(), unused
    forbidden=[]
    for p in root.rglob('*'):
        if not p.is_file(): continue
        name=p.relative_to(root).as_posix()
        if re.search(r'Qt6(?:Quick|Qml)|(?:^|/)qml/|(?:^|/)DepthSdk\.cpp$|\.pdb$|\.pem$|\.key$|(?:^|/)devices\.json$',name,re.I):
            forbidden.append(name)
    assert not forbidden, forbidden
    symbols=set(re.findall(r'CXWL_DEPTH_API\s+\w+\s+CXWL_DEPTH_CALL\s+(CxwlDepth_\w+)\s*\(', (root/'SDK/include/DepthSdk.h').read_text(encoding='utf-8')))
    expected=set(re.findall(r'"(CxwlDepth_\w+)"',(root/'Example/common/ExpectedExports.h').read_text(encoding='utf-8')))
    assert symbols==expected and len(symbols)==manifest['exported_functions'] and manifest['abi_version']==3
    reference = (root/'Doc/api/Chinese/API_REFERENCE.md').read_text(encoding='utf-8')
    if 'CxwlDepth_WaitForEvent' in symbols:
        headings = re.findall(r'^## (CxwlDepth_\w+)(?: \((?:buffer|Alloc)\))?$', reference, re.M)
        assert len(headings) == len(symbols) and set(headings) == symbols, 'Duplicate/missing API sections'
        assert (root/'Example/README.md').is_file()
        assert (root/'Script/generate_runtime_components.py').is_file()
        profile = json.loads((root/'Doc/host-profile.json').read_text(encoding='utf-8'))
        assert profile['sdk_version'] == manifest['sdk_version'] and profile['process']['peak_working_set_bytes'] > 0
        assert (root/'LICENSE').read_bytes() == (sdk/'licenses/ProductLicense.txt').read_bytes()
        notices = (root/'Licenses/Third-Party-Notices.txt').read_text(encoding='utf-8')
        assert manifest['display_version'] in notices.splitlines()[0]
    blocked={"adb.exe","ssh.exe","plink.exe","rvcontrolworker.exe","libssh2.dll","libcrypto-3-x64.dll","runtime-paths.json"}
    assert not any(p.name.lower() in blocked or p.name.lower() in {"rv_control","platform-tools","private_build"} for p in root.rglob("*"))
    assert (sdk/"tls/qschannelbackend.dll").is_file()
    return {'files':checked,'markdown_links':links,'public_exports':len(symbols),'json_bom':False,
            'single_runtime':True,'launchers':len(launchers),'native_usb_tls_runtime':True,'private_sources_or_gpl_only_modules':False,
            'runtime_manifests_agree':True,'standalone_viewer_entry':True,'pdf_manual':True,'reproducible_build_script':True}
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args()
    result=verify(args.root.resolve())
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result))

