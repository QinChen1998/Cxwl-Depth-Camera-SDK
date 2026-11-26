"""Build and run real C/C++ consumers of each deployment tier and relocated install."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

def validate(package, output, cmake):
    repo=Path(__file__).resolve().parents[1]
    output.mkdir(parents=True,exist_ok=False)
    source=output/'consumer';source.mkdir()
    (source/'CMakeLists.txt').write_text(r'''
cmake_minimum_required(VERSION 3.21)
project(CxwlIntegration LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(CxwlDepthSdk 3.0 CONFIG REQUIRED)
add_executable(consumer "@CPP@")
target_compile_options(consumer PRIVATE /UNDEBUG)
target_link_libraries(consumer PRIVATE CXWL::DepthSdk)
cxwl_deploy_runtime(consumer PROFILE @{PROFILE})
add_executable(c_consumer "@SELFTEST@")
target_include_directories(c_consumer PRIVATE "@COMMON@")
target_link_libraries(c_consumer PRIVATE CXWL::DepthSdk)
cxwl_deploy_runtime(c_consumer PROFILE @{PROFILE})
install(TARGETS consumer c_consumer RUNTIME DESTINATION bin)
cxwl_install_runtime(PROFILE @{PROFILE})
'''.replace('@{',chr(36)+'{').replace('@CPP@',(repo/'tests/test_sdk_cpp_helpers.cpp').as_posix())
        .replace('@SELFTEST@',(package/'Example/c/SelfTest/SelfTest.c').as_posix())
        .replace('@COMMON@',(package/'Example/common').as_posix()),encoding='utf-8')
    (output/'fixture.json').write_text('{"schema":1,"kind":"cxwl-depth-camera-devices","active":"","devices":[]}',encoding='utf-8')
    reports=[]
    def run(name,args,env=None,cwd=None,expected=0):
        with (output/(name+'.log')).open('wb') as log:
            result=subprocess.run([str(v) for v in args],stdout=log,stderr=subprocess.STDOUT,env=env,cwd=cwd,timeout=240)
        if result.returncode!=expected:
            raise RuntimeError(f'{name}: exit {result.returncode}, expected {expected}; see log')
    def clean_env(folder):
        env={k:v for k,v in os.environ.items() if not k.upper().startswith(('RV_','DCTS_','QT_','QML','CONDA','PYTHON'))}
        env.update(PATH=str(folder)+os.pathsep+str(Path(os.environ['SystemRoot'])/'System32'),
                   RV_TEST_OFFLINE_ONLY='1',TEMP=str(output),TMP=str(output))
        return env
    def consumer(label,prefix,profile):
        build=output/label
        run(label+'-configure',[cmake,'-S',source,'-B',build,'-G','Visual Studio 17 2022','-A','x64',
                               '-DCMAKE_SYSTEM_VERSION=10.0.22621.0',(f'-DCMAKE_PREFIX_PATH={prefix.parents[1]}' if label=='relocated' else f'-DCxwlDepthSdk_DIR={prefix}'),f'-DPROFILE={profile}'])
        run(label+'-build',[cmake,'--build',build,'--config','Release'])
        bins=build/'Release'
        run(label+'-cpp',[bins/'consumer.exe',output/'fixture.json'],env=clean_env(bins),cwd=output)
        run(label+'-c',[bins/'c_consumer.exe'],env=clean_env(bins),cwd=output)
        assert (bins/'Qt6Widgets.dll').exists()==(profile=='FULL')
        assert (bins/'avcodec-61.dll').exists()==(profile!='MINIMAL')
        assert not (bins/'platform-tools').exists()
        assert not (bins/'rv_control').exists()
        reports.append({'case':label,'profile':profile,'c':True,'cpp':True,'clean_path':True})
        print('PASS '+label,flush=True)
        return build
    for profile in ('MINIMAL','STANDARD','FULL'):
        consumer('direct-'+profile.lower(),package/'SDK/cmake',profile)
    # Repeated deployment must preserve unchanged timestamps and customer files,
    # while replacing a changed SDK file without launching a process per file.
    deployment=output/'direct-minimal/Release'
    customer_file=deployment/'customer-owned.txt'; customer_file.write_text('keep',encoding='utf-8')
    dll=deployment/'CxwlDepthSdk.dll'; initial=dll.read_bytes(); stamp=dll.stat().st_mtime_ns
    deploy=[cmake,f'-DSOURCE={package/"SDK/bin"}',f'-DDESTINATION={deployment}',
            '-DPROFILE=MINIMAL','-P',package/'SDK/cmake/DeployRuntime.cmake']
    run('deploy-unchanged',deploy)
    assert dll.stat().st_mtime_ns==stamp and customer_file.read_text()=='keep'
    dll.write_bytes(initial+b'changed-fixture')
    run('deploy-repair',deploy)
    assert dll.read_bytes()==initial and customer_file.read_text()=='keep'
    install_build=output/'install-build';installed=output/'installed';relocated=output/'relocated prefix'
    run('sdk-configure',[cmake,'-S',package/'SDK','-B',install_build,'-G','Visual Studio 17 2022','-A','x64',
                          '-DCMAKE_SYSTEM_VERSION=10.0.22621.0',f'-DCMAKE_INSTALL_PREFIX={installed}',
                          '-DCXWL_RUNTIME_PROFILE=STANDARD'])
    run('sdk-install',[cmake,'--install',install_build,'--config','Release'])
    shutil.copytree(installed,relocated)
    exported=relocated/'share/CxwlDepthSdk/CxwlDepthSdkTargets.cmake'
    assert exported.is_file()
    for config in (relocated/'share/CxwlDepthSdk').glob('*.cmake'):
        data=config.read_text(encoding='utf-8')
        assert str(installed).replace('\\','/') not in data
        assert str(package).replace('\\','/') not in data
    build=consumer('relocated',exported.parent,'STANDARD')
    app=output/'app-install'
    run('app-install',[cmake,'--install',build,'--config','Release','--prefix',app])
    run('app-install-cpp',[app/'bin/consumer.exe',output/'fixture.json'],env=clean_env(app/'bin'),cwd=output)
    run('app-install-c',[app/'bin/c_consumer.exe'],env=clean_env(app/'bin'),cwd=output)
    network=output/'network-only'
    run('network-configure',[cmake,'-S',package/'SDK','-B',output/'network-install-build',
        '-G','Visual Studio 17 2022','-A','x64','-DCMAKE_SYSTEM_VERSION=10.0.22621.0',
        f'-DCMAKE_INSTALL_PREFIX={network}','-DCXWL_RUNTIME_PROFILE=STANDARD'])
    run('network-install',[cmake,'--install',output/'network-install-build','--config','Release'])
    assert not (network/'bin/platform-tools').exists()
    consumer('registered-network',network/'share/CxwlDepthSdk','STANDARD')
    old=output/'old-version';old.mkdir()
    (old/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.21)\nproject(OldABI LANGUAGES C)\nfind_package(CxwlDepthSdk 1.0 CONFIG REQUIRED)\n',encoding='utf-8')
    run('old-abi-rejected',[cmake,'-S',old,'-B',output/'old-version-build','-G','Visual Studio 17 2022','-A','x64',
                           f'-DCxwlDepthSdk_DIR={package/"SDK/cmake"}'],expected=1)
    probe = output / 'package-probe'
    probe.mkdir()
    (probe / 'CMakeLists.txt').write_text(r'''
cmake_minimum_required(VERSION 3.21)
project(PackageProbe LANGUAGES NONE)
find_package(CxwlDepthSdk CONFIG QUIET)
if(EXPECT_FOUND AND NOT CxwlDepthSdk_FOUND)
  message(FATAL_ERROR "Expected x64 package in a project without enabled languages")
elseif(NOT EXPECT_FOUND AND CxwlDepthSdk_FOUND)
  message(FATAL_ERROR "Incompatible platform matched")
endif()
''', encoding='utf-8')
    for arch in ('x64', 'Win32'):
        run('quiet-probe-' + arch, [cmake, '-S', probe, '-B', output / ('probe-' + arch),
            '-G', 'Visual Studio 17 2022', '-A', arch, f'-DCxwlDepthSdk_DIR={package/"SDK/cmake"}',
            '-DEXPECT_FOUND=' + ('ON' if arch == 'x64' else 'OFF')])

    custom = output / 'custom-deploy'
    custom.mkdir()
    (custom / 'main.c').write_text('#include "DepthSdk.h"\nint main(void) { return CxwlDepth_AbiVersion() == CXWL_DEPTH_ABI_VERSION ? 0 : 1; }\n', encoding='utf-8')
    (custom / 'CMakeLists.txt').write_text(r'''
cmake_minimum_required(VERSION 3.21)
project(CustomRuntime LANGUAGES C)
find_package(CxwlDepthSdk CONFIG REQUIRED)
add_executable(custom main.c)
target_link_libraries(custom PRIVATE CXWL::DepthSdk)
cxwl_deploy_runtime(custom PROFILE MINIMAL DESTINATION "private runtime")
''', encoding='utf-8')
    custom_build = output / 'custom-build'
    run('custom-configure', [cmake, '-S', custom, '-B', custom_build, '-G', 'Visual Studio 17 2022',
                             '-A', 'x64', f'-DCxwlDepthSdk_DIR={package/"SDK/cmake"}'])
    run('custom-build', [cmake, '--build', custom_build, '--config', 'Release'])
    custom_bin = custom_build / 'Release'
    assert not (custom_bin / 'CxwlDepthSdk.dll').exists()
    assert (custom_bin / 'private runtime/CxwlDepthSdk.dll').is_file()
    run('custom-directory-consumer', [custom_bin / 'custom.exe'], env=clean_env(custom_bin / 'private runtime'), cwd=output)
    launchers=package/'Example/bin'
    run('launcher-selftest',[launchers/'CxwlDepthSelfTest.exe'],env=clean_env(launchers),cwd=output)
    run('launcher-args',[launchers/'CxwlDepthQuickStart.exe','invalid mode with spaces'],env=clean_env(launchers),cwd=output,expected=2)
    cloud=output/'launch 工作目录';cloud.mkdir()
    run('launcher-pointcloud',[launchers/'CxwlDepthPointCloud.exe','--help'],env=clean_env(launchers),cwd=cloud)
    assert not (output/'cloud.ply').exists() and not (output/'depth.pgm').exists()
    self_test = cloud/'自检结果.json'
    run('launcher-report',[launchers/'CxwlDepthSelfTest.exe','-v','-Report',self_test],env=clean_env(launchers),cwd=output)
    report = json.loads(self_test.read_text(encoding='utf-8'))
    assert report['passed'] and not report['missing_exports'] and all(report['checks'].values())
    result={'passed':True,'profiles':reports,'install_export':True,'relocation':True,
            'consumer_install':True,'abi1_find_rejected':True,'launcher_arguments_cwd_exit':True,
            'hardware_started':False,'deployment_idempotent_preserves_customer_files':True,
            'registered_network_install_excludes_adb':True,'quiet_platform_probe':True,'custom_deploy_destination':True}
    (output/'summary.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result),flush=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--cmake',type=Path,required=True)
    args=parser.parse_args()
    validate(args.package.resolve(),args.output.resolve(),args.cmake.resolve())

