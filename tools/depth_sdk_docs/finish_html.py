"""Add Chinese topic keywords to the Doxygen 1.9.7 offline search and menu."""
import argparse
import json
import re
from html import escape
from pathlib import Path


# The native search is a prefix index. Give frequent Chinese queries a searchable prefix.
TOPICS = {
    "sdk_lifecycle": ("连接与释放", ("首帧", "订阅", "回调", "生命周期")),
    "sdk_specifications": ("测量规格", ("帧率", "时延", "探测距离", "重复性", "FPS")),
    "sdk_data": ("数据与坐标", ("点云", "坐标", "质量图", "深度")),
    "sdk_usb": ("USB 接入", ("驱动", "登记", "证书", "RNDIS")),
    "sdk_build": ("构建与部署", ("部署", "CMake", "安装")),
    "sdk_errors": ("状态与排障", ("错误码", "恢复", "排障")),
    "sdk_examples": ("示例", ("示例", "QuickStart", "Viewer")),
    "sdk_json": ("JSON 字段", ("快照", "JSON")),
    "sdk_terminology": ("术语", ("术语", "模式编号")),
    "deprecated": ("弃用接口", ("弃用", "迁移")),
}
GLOBAL_TITLES = {"globals": "全局索引", "globals_defs": "宏定义", "globals_eval": "枚举值",
                 "globals_func": "全局函数", "globals_type": "类型定义"}


def extend_search(directory):
    index = directory / "searchdata.js"
    data = index.read_text(encoding="utf-8")
    for group in ("all", "pages"):
        section = re.search(r'(\d+): "' + group + r'"', data).group(1)
        pattern = re.compile(r'(^\s*' + section + r': ")([^"]*)(")', re.M)
        match = pattern.search(data)
        letters = match[2]
        buckets = {}
        for page, (title, keywords) in TOPICS.items():
            for keyword in keywords:
                first = keyword[0].lower()
                buckets.setdefault(first, []).append(
                    [keyword.lower() + "_topic", [keyword + " · " + title, ["../" + page + ".html", 1, ""]]]
                )
        for first, entries in buckets.items():
            new = first not in letters
            if new:
                letters += first
            path = directory / f"{group}_{letters.index(first):x}.js"
            data_js = "var searchData=[];\n" if new else path.read_text(encoding="utf-8")
            path.write_text(data_js + "searchData.push(" + ", ".join(json.dumps(entry, ensure_ascii=False)
                            for entry in entries) + ");\n", encoding="utf-8")
        data = data[:match.start(2)] + letters + data[match.end(2):]
    # A build into an existing output can otherwise retain old, unreferenced search buckets.
    content, names = data.split("var indexSectionNames", 1)
    names = names.split("var indexSectionLabels", 1)[0]
    sections = dict(re.findall(r'(\d+): "([^"]*)"', content))
    indexed = {f"{name}_{i:x}.js" for section, name in re.findall(r'(\d+): "([^"]+)"', names)
               for i in range(len(sections[section]))}
    for path in directory.glob("*_*.js"):
        if re.fullmatch(r"[a-z]+_[0-9a-f]+\.js", path.name) and path.name not in indexed:
            path.unlink()
    index.write_text(data, encoding="utf-8")


def finish(directory, product):
    for page in TOPICS:
        if not (directory / (page + ".html")).is_file():
            raise ValueError(f"Missing guide page: {page}")
    menu = directory / "menudata.js"
    guides = {"text": "开发指南", "url": "pages.html", "children": [
        {"text": title, "url": page + ".html"} for page, (title, _) in TOPICS.items()]}
    with menu.open("a", encoding="utf-8") as out:
        out.write("\nmenudata.children.push(" + json.dumps(guides, ensure_ascii=False) + ");\n")
    extend_search(directory / "search")
    footer = (f'<footer style="padding:12px;text-align:center;color:#526879">'
              f'SDK {escape(product["sdk_version"])} / ABI v{product["abi_version"]}</footer>')
    for page in directory.glob("*.html"):
        html = page.read_text(encoding="utf-8")
        if 'name="generator" content="Doxygen ' not in html:
            continue
        # Older runs with EXTRACT_ALL or unstripped paths left empty file/directory pages.
        if page.name.endswith("_8md.html") or re.fullmatch(r"dir_[0-9a-f]{32}\.html", page.name):
            page.unlink()
            continue
        if page.stem in GLOBAL_TITLES:
            html = re.sub(r"(<title>[^<]*?)全局定义(</title>)",
                          r"\g<1>" + GLOBAL_TITLES[page.stem] + r"\2", html)
        html = html.replace("制作者&#160;", "文档生成器&#160;")
        page.write_text(html.replace("</body>", footer + "\n</body>"), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("product", type=Path)
    args = parser.parse_args()
    finish(args.output, json.loads(args.product.read_text(encoding="utf-8")))
