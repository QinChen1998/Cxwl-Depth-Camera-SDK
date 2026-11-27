"""Build an offline Linux API manual with the platform's USB and build guides."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from build_sdk_pdf import build as render_pdf
from depth_sdk_docs.finish_html import finish
from export_sdk_markdown import generate


def build(args):
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    product = json.loads((ROOT / "sdk/product.json").read_text(encoding="utf-8"))
    architecture = {"x64": ("Linux x86-64", "__x86_64__"), "arm64": ("Linux ARM64", "__aarch64__")}
    platform, macro = architecture[args.arch]
    edition = "USB 直连 · 1.0.0-preview.2-usb-direct"
    with tempfile.TemporaryDirectory() as folder:
        source = Path(folder)
        headers = source / "sdk"
        (headers / "examples").mkdir(parents=True)
        for name in ("DepthSdk.h", "DepthSdkVersion.h", "product.json", "DepthSdkExample.c", "examples/DepthQuality.c"):
            shutil.copyfile(ROOT / "sdk" / name, headers / name)
        pages = source / "docs/sdk"
        shutil.copytree(ROOT / "docs/sdk", pages)
        linux = (ROOT / "docs/DEPTH_SDK_LINUX.md").read_text(encoding="utf-8")
        section = {part.partition("\n")[0]: part.partition("\n")[2].strip() for part in linux.split("\n## ")[1:]}
        migration_heading = "## 传输安全与迁移"
        usb_guide = (pages / "usb.md").read_text(encoding="utf-8")
        migration = usb_guide.split(migration_heading, 1)[1].split("\n## ", 1)[0].strip()
        linux_usb = section["接入相机"].replace(" [安全边界与档案迁移](sdk/usb.md)", "")
        supplements = {
            "index": "# Linux 深度相机 SDK\n\n使用 C ABI v3 连接预装设备服务的 RV1103 相机。\n\n@ref sdk_usb\n\n@ref sdk_build\n\n@ref sdk_lifecycle\n",
            "usb": f"@page sdk_usb USB 登记、网络与凭据\n\n{linux_usb}\n\n{migration_heading}\n\n{migration}",
            "build": "@page sdk_build Linux 构建和部署\n\n" + section["构建客户应用与样例"] + "\n\n" + section["校验交付"],
            "examples": "@page sdk_examples C/C++ 示例\n\n" + section["构建客户应用与样例"],
            "errors": "@page sdk_errors 状态与故障处理\n\n通过 `CxwlDepth_GetSessionStatus` 和 `CxwlDepth_GetStateAlloc` 查询采集状态。在同一所有者线程派发事件，等待停采回执后释放会话。连接失败时检查 USB 网卡、相机登记和设备服务版本。\n\n@ref sdk_usb\n",
            "rgb_lsc": "@page sdk_rgb_lsc SC3336 双绿矫正网格\n\n采集 SDK 使用板端现有 RGB 参数。RGB 校正由配套的独立校正工具完成。\n",
        }
        for name, text in supplements.items():
            (pages / (name + ".md")).write_text(text, encoding="utf-8")
        for name in ("terminology", "specifications", "lifecycle", "json"):
            path = pages / (name + ".md")
            text = path.read_text(encoding="utf-8").replace("Windows 3.0.0 开发版", "SDK 3.0.0")
            text = text.replace("当前 Windows 用户", "当前 Linux 用户").replace("Windows x64", platform)
            text = text.replace("Windows 包在 `Example/bin` 提供示例启动器。", "Linux 包在 `Example/bin` 提供可执行样例。")
            text = text.replace("Windows 三种部署档位包含 SDK、TLS 及各档所需的图像或视频运行库。",
                                "Linux 的 STANDARD 部署包含 SDK、TLS 插件和图像或视频运行库。")
            path.write_text(text, encoding="utf-8")

        config = (ROOT / "tools/depth_sdk_docs/Doxyfile").read_text(encoding="utf-8")
        config = config.replace("_WIN32=1 _M_X64=100", f"__linux__=1 {macro}=1")
        env = dict(os.environ, CXWL_DOC_ROOT=source.as_posix(), CXWL_DOC_OUTPUT=output.as_posix(),
                   CXWL_DOC_VERSION=product["sdk_version"])
        result = subprocess.run([args.doxygen, "-"], input=config.encode(), env=env)
        warnings = (output / "warnings.log").read_text(encoding="utf-8")
        if result.returncode or warnings.strip():
            raise RuntimeError(warnings or f"Doxygen exited with {result.returncode}")
        xml = output / "xml/_depth_sdk_8h.xml"
        header = (headers / "DepthSdk.h").read_bytes()
        exports = set(re.findall(rb"CXWL_DEPTH_API\s+\w+\s+CXWL_DEPTH_CALL\s+(CxwlDepth_\w+)", header))
        documented = {node.text.encode() for node in ET.parse(xml).findall('.//memberdef[@kind="function"]/name')}
        if exports != documented:
            raise ValueError("Doxygen function list differs from the ABI header")
        markdown = output / "API_REFERENCE.md"
        generate(xml, markdown, source, platform, edition)
        shutil.copytree(pages / "schemas", output / "schemas")
        finish(output, product)
        render_pdf(markdown, output / "API_REFERENCE.pdf", args.font, args.bold_font, platform, edition=edition)
        render_pdf(ROOT / "docs/POINT_CLOUD_RECORDING.md", output / "PointCloudRecording.pdf",
                   args.font, args.bold_font, platform, handout=True)
    shutil.rmtree(output / "xml")
    (output / "warnings.log").unlink()
    receipt = {"platform": platform, "sdk_version": product["sdk_version"], "abi_version": product["abi_version"],
               "public_header_sha256": hashlib.sha256(header).hexdigest(), "functions": len(exports)}
    (output / "doxygen-build.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(output / "index.html")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=("x64", "arm64"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--doxygen", default="doxygen")
    parser.add_argument("--font", type=Path, required=True)
    parser.add_argument("--bold-font", type=Path, required=True)
    build(parser.parse_args())
