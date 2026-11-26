"""Run ABI, JSON and real consumer checks on a native Linux acceptance target."""

import argparse
import json
import os
from pathlib import Path
import platform
import shlex
import subprocess
import sys

from runtime import ARCHITECTURES, check_exports, elf_machine, sha256, standard_files, standard_hash


ROOT = Path(__file__).resolve().parents[2]


def validate(stage, output, arch):
    expected_elf, machine = ARCHITECTURES[arch]
    if platform.system() != "Linux" or platform.machine() != machine:
        raise ValueError(f"Run the offline checks on Linux {machine}")
    output.mkdir(parents=True, exist_ok=False)
    sdk = stage / "SDK"
    library = sdk / "lib/libCxwlDepthSdk.so.3"
    examples = stage / "Example"
    product = json.loads((ROOT / "sdk/product.json").read_text(encoding="utf-8"))
    env = {k: v for k, v in os.environ.items() if k not in ("LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH")}
    env.update(RV_TEST_OFFLINE_ONLY="1", XDG_DATA_HOME=str(output / "user-data"), LC_ALL="C.UTF-8")
    checks = []

    def run(name, command, success=True, extra=None):
        with (output / (name + ".log")).open("w", encoding="utf-8") as log:
            result = subprocess.run(command, cwd=output, env=dict(env, **(extra or {})), stdout=log, stderr=subprocess.STDOUT)
        if (result.returncode == 0) != success:
            raise RuntimeError(f"{name} returned {result.returncode}: see {log.name}")
        checks.append(name)

    exports = check_exports(sdk)
    for binary in [*(sdk / "lib" / path for path in standard_files(sdk)), *(examples / "bin").iterdir()]:
        if elf_machine(binary) != expected_elf:
            raise ValueError(f"ELF machine mismatch: {binary.name}")
        dependencies = subprocess.check_output(["ldd", binary], env=env, text=True)
        if "not found" in dependencies:
            raise RuntimeError(f"Unresolved ELF dependency in {binary.name}: {dependencies}")
    checks.append("elf-architecture-exports-dependencies")

    run("packaged-selftest", [examples / "bin/CxwlDepthSelfTest", "-Report", output / "selftest.json"])
    selftest = json.loads((output / "selftest.json").read_text(encoding="utf-8"))
    if not selftest["passed"] or selftest["sdk_version"] != product["sdk_version"] or selftest["abi_version"] != product["abi_version"]:
        raise ValueError("SDK self-test did not match the current product")
    run("public-abi", [sys.executable, "-B", ROOT / "tests/test_depth_sdk_abi.py", "--dll", library, "--output", output / "abi"])
    run("json-contract", [sys.executable, "-B", ROOT / "tests/test_sdk_json_contract.py", "--dll", library, "--output", output / "json"])
    run("offline-device-list", [examples / "bin/CxwlDepthDeviceManagement", "list"])
    run("offline-usb-blocked", [examples / "bin/CxwlDepthDeviceManagement", "scan"], success=False)

    rebuild = output / "customer-build"
    run("customer-configure", ["cmake", "-S", examples, "-B", rebuild, "-DCMAKE_BUILD_TYPE=Release"])
    run("customer-build", ["cmake", "--build", rebuild, "--parallel", "2"])
    run("customer-selftest", [rebuild / "CxwlDepthSelfTest"])

    source = output / "consumer.c"
    source.write_text('#include <DepthSdk.h>\nint main(void) { return CxwlDepth_AbiVersion() != CXWL_DEPTH_ABI_VERSION; }\n', encoding="utf-8")
    pc = dict(env, PKG_CONFIG_PATH=str(sdk / "lib/pkgconfig"))
    flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "cxwl-depth-sdk"], env=pc, text=True))
    for standard in ("c99", "c11"):
        binary = output / ("consumer-" + standard)
        run("pkg-config-" + standard, ["cc", "-std=" + standard, source, "-o", binary, *flags, f"-Wl,-rpath,{sdk / 'lib'}"])
        run("consumer-" + standard, [binary])

    return {"passed": True, "architecture": machine, "sdk_version": product["sdk_version"],
            "abi_version": product["abi_version"], "exports": exports, "checks": checks,
            "library_sha256": sha256(library), "standard_runtime_sha256": standard_hash(sdk),
            "example_sha256": {path.name: sha256(path) for path in sorted((examples / "bin").iterdir())}}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=ARCHITECTURES, required=True)
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = validate(args.stage.resolve(), args.output.resolve(), args.arch)
    text = json.dumps(report, indent=2) + "\n"
    (args.output / "summary.json").write_text(text, encoding="utf-8")
    print(f"{args.arch}: {len(report['checks'])} offline checks, {report['exports']} exports")
