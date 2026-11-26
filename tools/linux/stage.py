"""Install a development runtime and build the seven customer examples for acceptance."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess

from runtime import ARCHITECTURES, check_exports, elf_machine


ROOT = Path(__file__).resolve().parents[2]
QT = ("libQt6Core.so.6", "libQt6Gui.so.6", "libQt6Network.so.6", "libQt6Concurrent.so.6", "libQt6DBus.so.6")
FFMPEG = ("libavcodec.so.61", "libavformat.so.61", "libavutil.so.59", "libswscale.so.8", "libswresample.so.5")
EXAMPLES = ("SelfTest.c", "DepthQuality.c", "PointCloud.c", "DeviceManagement.c", "CppQuickStart.cpp",
            "ThreadedCapture.cpp", "ExpectedExports.h", "SampleJson.h", "SampleSupport.h")


def install_dependencies(sdk, deps):
    lib = sdk / "lib"
    vendors = lib / "cxwl"
    tls = lib / "tls"
    vendors.mkdir()
    tls.mkdir()
    for prefix, names in ((deps / "qt-6.11.1", QT), (deps / "ffmpeg-7.1.3", FFMPEG)):
        for name in names:
            source = prefix / "lib" / name
            if not source.is_file():
                source = prefix / "lib64" / name
            destination = vendors / name
            shutil.copyfile(source, destination)
            subprocess.run(["patchelf", "--set-rpath", "$ORIGIN", destination], check=True)
    plugin = tls / "libqopensslbackend.so"
    shutil.copyfile(deps / "qt-6.11.1/plugins/tls" / plugin.name, plugin)
    subprocess.run(["patchelf", "--set-rpath", "$ORIGIN/../cxwl", plugin], check=True)
    files = sorted(path.relative_to(lib).as_posix() for directory in (vendors, tls)
                   for path in directory.iterdir() if path.is_file())
    files.extend(path.name for path in sorted(lib.glob("libCxwlDepthSdk.so*")))
    value = {"profiles": {"STANDARD": {"files": sorted(files)}}}
    (sdk / "runtime-components.json").write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def install_examples(stage, build, toolchain):
    example = stage / "Example"
    for folder in ("common", "c", "cpp"):
        (example / folder).mkdir(parents=True, exist_ok=True)
    for name in EXAMPLES:
        folder = {".h": "common", ".c": "c", ".cpp": "cpp"}[Path(name).suffix]
        shutil.copyfile(ROOT / "sdk/examples" / name, example / folder / name)
    shutil.copyfile(ROOT / "sdk/DepthSdkExample.c", example / "c/DepthSdkExample.c")
    shutil.copyfile(ROOT / "tools/linux/Examples.CMakeLists.txt", example / "CMakeLists.txt")
    shutil.copyfile(ROOT / "tools/sdk_package/Example.LICENSE", example / "LICENSE")
    command = ["cmake", "-S", example, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release"]
    if toolchain:
        command.append(f"-DCMAKE_TOOLCHAIN_FILE={toolchain.resolve()}")
    subprocess.run(command, check=True)
    subprocess.run(["cmake", "--build", build, "--parallel", "3"], check=True)
    subprocess.run(["cmake", "--install", build, "--prefix", example], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=ARCHITECTURES, required=True)
    for name in ("build", "deps", "output", "examples-build"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--toolchain", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    sdk = args.output / "SDK"
    subprocess.run(["cmake", "--install", args.build, "--prefix", sdk], check=True)
    install_dependencies(sdk, args.deps)
    install_examples(args.output, args.examples_build, args.toolchain)
    shutil.copytree(ROOT / "docs/sdk/schemas", args.output / "Doc/schemas")
    scripts = args.output / "Script"
    scripts.mkdir()
    shutil.copyfile(ROOT / "tools/linux/cxwl-usb-network", scripts / "cxwl-usb-network")
    expected = ARCHITECTURES[args.arch][0]
    binaries = [*(sdk / "lib").rglob("*.so*"), *(args.output / "Example/bin").iterdir()]
    for binary in binaries:
        if elf_machine(binary) != expected:
            raise ValueError(f"Mixed ELF architectures: {binary.name}")
    print(f"{args.arch}: {len(binaries)} ELF files, {check_exports(sdk)} public C exports")


if __name__ == "__main__":
    main()
