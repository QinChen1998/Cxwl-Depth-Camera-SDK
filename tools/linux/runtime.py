"""Shared verification of the Linux SDK ELF binaries and runtime fingerprint."""

import hashlib
import json
from pathlib import Path
import re
import subprocess


ARCHITECTURES = {"x64": (62, "x86_64"), "arm64": (183, "aarch64")}


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as file:
        for block in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def elf_machine(path):
    with path.open("rb") as file:
        header = file.read(20)
    if len(header) != 20 or not header.startswith(b"\x7fELF\x02\x01\x01"):
        raise ValueError(f"Expected a 64-bit little-endian ELF file: {path.name}")
    return int.from_bytes(header[18:20], "little")


def check_exports(sdk):
    header = (sdk / "include/DepthSdk.h").read_text(encoding="utf-8")
    declared = set(re.findall(r"CXWL_DEPTH_API\s+\w+\s+CXWL_DEPTH_CALL\s+(CxwlDepth_\w+)\s*\(", header))
    symbols = subprocess.check_output(["nm", "-D", "--defined-only", "-P", sdk / "lib/libCxwlDepthSdk.so"], text=True)
    exported = {line.split()[0] for line in symbols.splitlines()}
    if not declared or declared != exported:
        raise ValueError(f"ELF exports differ from the public C header: {sorted(declared ^ exported)}")
    return len(exported)


def standard_files(sdk):
    paths = json.loads((sdk / "runtime-components.json").read_text(encoding="utf-8"))["profiles"]["STANDARD"]["files"]
    lib = (sdk / "lib").resolve()
    if not paths or len(set(paths)) != len(paths):
        raise ValueError("Invalid STANDARD runtime manifest")
    for name in paths:
        path = lib / name
        if not path.resolve().is_relative_to(lib) or not path.is_file():
            raise ValueError(f"Invalid or missing runtime component: {name}")
    return sorted(paths)


def standard_hash(sdk):
    rows = (f"{name}\t{sha256(sdk / 'lib' / name)}\n" for name in standard_files(sdk))
    return hashlib.sha256("".join(rows).encode()).hexdigest()
