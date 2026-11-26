"""Create and recheck a Linux deliverable after matching offline and physical acceptance."""

import argparse
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tarfile
import tempfile

from runtime import ARCHITECTURES, check_exports, sha256, standard_hash


ROOT = Path(__file__).resolve().parents[2]
MODES = ("board", "high", "8x8", "8x8-high", "16x16", "16x16-high")
OFFLINE_CHECKS = {"elf-architecture-exports-dependencies", "packaged-selftest", "public-abi", "json-contract",
                  "offline-device-list", "offline-usb-blocked", "customer-configure", "customer-build",
                  "customer-selftest", "pkg-config-c99", "consumer-c99", "pkg-config-c11", "consumer-c11"}


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def check_acceptance(stage, arch, offline, live, samples, product):
    sdk = stage / "SDK"
    version = product["sdk_version"]
    abi = product["abi_version"]
    machine = ARCHITECTURES[arch][1]
    library = sha256(sdk / "lib/libCxwlDepthSdk.so.3")
    runtime = standard_hash(sdk)
    exports = check_exports(sdk)
    for report in (offline, live):
        observed = tuple(report.get(field) for field in ("sdk_version", "abi_version", "architecture",
                                                        "library_sha256", "standard_runtime_sha256"))
        if observed != (version, abi, machine, library, runtime):
            raise ValueError("Acceptance does not match this architecture and these runtime bytes")
    examples = {path.name: sha256(path) for path in sorted((stage / "Example/bin").iterdir())}
    if (not offline.get("passed") or not OFFLINE_CHECKS.issubset(offline.get("checks", []))
            or offline.get("exports") != exports or len(examples) != 7 or offline.get("example_sha256") != examples):
        raise ValueError("The customer examples or offline acceptance checks are incomplete")
    modes = live.get("modes", [])
    if (live.get("platform") != "Linux" or tuple(row["mode"] for row in modes) != MODES
            or any(not all(row.get(key) for key in ("released", "stopped", "ply_matches_board_bytes", "single_generation"))
                   or min(row["rgb_frames"], row["depth_frames"], row["quality_frames"], row["valid_depth_pixels_max"]) <= 0
                   or not row["depth_frames"] == row["quality_frames"] == row["board_clouds"] for row in modes)):
        raise ValueError("All six modes need an RGB, depth, quality, XYZ and confirmed stop receipt")
    transitions = live.get("transitions", {})
    operations = [step["operation"] for step in transitions.get("steps", [])]
    steps = [step["mode"] for step in transitions.get("steps", [])]
    if (not transitions.get("released") or transitions.get("distinct_generations") != 8
            or operations != ["connect", *(["switch"] * 5), "reconnect", "restart"]
            or steps != [1, 2, 3, 4, 5, 6, 6, 6]):
        raise ValueError("Physical transition and release acceptance is incomplete")
    if not samples.get("passed") or samples.get("cpp_points") != 1536 or samples.get("pointcloud_valid_points", 0) <= 0:
        raise ValueError("Physical C and C++ sample acceptance is incomplete")
    return {"sdk_version": version, "abi_version": abi, "platform": f"linux-{arch}",
            "exports": exports, "standard_runtime_sha256": runtime, "hardware_connected_for_validation": True}


def check_windows_reference(stage, reference, product):
    metadata = read_json(reference / "sdk-manifest.json")
    if (metadata["sdk_version"], metadata["abi_version"], metadata["platform"]) != (product["sdk_version"],
                                                                                    product["abi_version"], "windows-x64"):
        raise ValueError("Windows release version or ABI does not match")
    for name in ("DepthSdk.h", "DepthSdk.hpp", "DepthSdkVersion.h"):
        key = f"SDK/include/{name}"
        if not sha256(reference / key) == metadata["sha256"][key] == sha256(stage / key):
            raise ValueError(f"Header differs from the Windows release: {name}")
    dll = reference / "SDK/bin/CxwlDepthSdk.dll"
    if sha256(dll) != metadata["sha256"]["SDK/bin/CxwlDepthSdk.dll"]:
        raise ValueError("Windows reference DLL checksum mismatch")
    dump = subprocess.check_output(["objdump", "-p", dll], text=True)
    symbols = subprocess.check_output(["nm", "-D", "--defined-only", "-P", stage / "SDK/lib/libCxwlDepthSdk.so"], text=True)
    elf = {line.split()[0] for line in symbols.splitlines()}
    if set(re.findall(r"\]\s+(CxwlDepth_\w+)\b", dump)) != elf:
        raise ValueError("Linux and Windows export sets differ")


def check_ffmpeg(stage):
    binary = (stage / "SDK/lib/cxwl/libavcodec.so.61").read_bytes()
    configurations = [entry for entry in binary.split(b"\0") if b"--disable-gpl" in entry and b"--disable-nonfree" in entry]
    if not configurations or any(b"--enable-gpl" in entry or b"--enable-nonfree" in entry for entry in configurations):
        raise ValueError("Could not verify the FFmpeg LGPL build configuration")


def copy_qt_sbom(source, destination):
    original = source.read_text(encoding="utf-8")
    public = re.sub(r"(?m)^[^\n<>]+ was configured with:\n[ \t]+qt6/configure[^\n]*\n(?=</text>)", "", original)
    if re.search(r"(?<![\w./-])/(?:root|home/[^/\s]+|mnt/[A-Za-z])/[\w.-]+", public):
        raise ValueError("The Qt SBOM still contains a local build-machine path")
    destination.write_text(public, encoding="utf-8")


def write_checksums(root):
    files = sorted(path for path in root.rglob("*") if path.is_file())
    for path in files:
        name = path.relative_to(root).as_posix()
        if (path.name.startswith(".env") or path.suffix in (".key", ".pem", ".pfx", ".dpapi")
                or path.name.endswith(".private.json") or not path.resolve().is_relative_to(root)):
            raise ValueError(f"Private or invalid package path: {name}")
    (root / "SHA256SUMS").write_text("".join(f"{sha256(path)}  {path.relative_to(root).as_posix()}\n" for path in files),
                                     encoding="utf-8")
    return len(files)


def verify_archive(archive, destination, name, expected):
    with tarfile.open(archive, "r:gz") as handle:
        members = handle.getmembers()
        names = set()
        for member in members:
            path = PurePosixPath(member.name)
            if (path.is_absolute() or not path.parts or path.parts[0] != name or ".." in path.parts or path in names
                    or not (member.isfile() or member.isdir() or member.issym())):
                raise ValueError(f"Invalid archive entry: {member.name}")
            if member.issym() and (not member.linkname or PurePosixPath(member.linkname).name != member.linkname
                                  or member.linkname in (".", "..")):
                raise ValueError(f"Invalid shared library link: {member.name}")
            names.add(path)
        handle.extractall(destination)
    root = destination / name
    subprocess.run(["sha256sum", "--strict", "--quiet", "--check", "SHA256SUMS"], cwd=root, check=True)
    if sum(path.is_file() for path in root.rglob("*")) != expected + 1:
        raise ValueError("The extracted SDK contains missing or additional files")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=ARCHITECTURES, required=True)
    for arg in ("stage", "offline", "live", "samples", "qt-licenses", "output"):
        parser.add_argument("--" + arg, type=Path, required=True)
    parser.add_argument("--windows-reference", type=Path)
    parser.add_argument("--manual", type=Path)
    parser.add_argument("--qt-sbom", type=Path)
    args = parser.parse_args()
    stage = args.stage.resolve()
    product = read_json(ROOT / "sdk/product.json")
    offline, live, samples = (read_json(path) for path in (args.offline, args.live, args.samples))
    receipt = check_acceptance(stage, args.arch, offline, live, samples, product)
    check_ffmpeg(stage)
    receipt["ffmpeg_lgpl_configuration_verified"] = True
    if args.windows_reference:
        check_windows_reference(stage, args.windows_reference.resolve(), product)
        receipt["windows_abi_and_exports_match"] = True
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    name = f"Cxwl-Depth-Camera-SDK-v{product['sdk_version']}-linux-{args.arch}"
    archive = output / (name + ".tar.gz")
    checksum = archive.with_suffix(archive.suffix + ".sha256")
    if archive.exists() or checksum.exists():
        raise FileExistsError(f"Choose a fresh output directory: {archive}")
    with tempfile.TemporaryDirectory(dir=output) as temporary:
        scratch = Path(temporary)
        root = scratch / name
        shutil.copytree(stage, root, symlinks=True)
        shutil.copyfile(ROOT / "LICENSE", root / "LICENSE")
        shutil.copyfile(ROOT / "docs/DEPTH_SDK_LINUX.md", root / "Doc/Linux-SDK.md")
        shutil.copyfile(ROOT / "docs/POINT_CLOUD_RECORDING.md", root / "Doc/PointCloudRecording.md")
        if args.manual:
            manual = read_json(args.manual / "doxygen-build.json")
            expected_platform = "Linux x86-64" if args.arch == "x64" else "Linux ARM64"
            if (manual.get("platform"), manual.get("sdk_version"), manual.get("abi_version"),
                    manual.get("public_header_sha256"), manual.get("functions")) != (
                    expected_platform, product["sdk_version"], product["abi_version"],
                    sha256(stage / "SDK/include/DepthSdk.h"), receipt["exports"]):
                raise ValueError("API manual does not match the SDK header and architecture")
            shutil.copytree(args.manual, root / "Doc/API")
        notices = root / "Licenses"
        notices.mkdir()
        shutil.copyfile(ROOT / "tools/linux/Third-Party-Notices.md", notices / "Third-Party-Notices.md")
        shutil.copyfile(ROOT / "third_party/ffmpeg_7_1_3/COPYING.LGPLv2.1", notices / "COPYING.LGPLv2.1")
        for license_name in ("GPL-3.0-only.txt", "LGPL-3.0-only.txt"):
            shutil.copyfile(args.qt_licenses / license_name, notices / license_name)
        if args.qt_sbom:
            copy_qt_sbom(args.qt_sbom, notices / "qtbase-6.11.1.spdx")
        write_json(root / "Doc/build-validation.json", receipt | {"offline": offline, "samples": samples})
        write_json(root / "Doc/device-validation.json", live)
        write_json(root / "sdk-manifest.json", receipt)
        files = write_checksums(root)
        candidate = scratch / archive.name
        with tarfile.open(candidate, "w:gz", compresslevel=6) as handle:
            handle.add(root, arcname=name)
        verify_archive(candidate, scratch / "verified", name, files)
        checksum_text = f"{sha256(candidate)}  {archive.name}\n"
        candidate.replace(archive)
        checksum.write_text(checksum_text, encoding="ascii")
    print(f"{archive} ({files} files verified after extraction)\n{checksum_text.strip()}")


if __name__ == "__main__":
    main()
