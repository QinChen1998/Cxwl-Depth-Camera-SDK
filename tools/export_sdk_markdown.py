"""Export the public API reference in Markdown from Doxygen XML."""
import argparse
import json
import re
from pathlib import Path
import xml.etree.ElementTree as ET


def text(element):
    return '' if element is None else ''.join(element.itertext()).strip()


def details(member):
    paragraphs = []
    for paragraph in member.findall('detaileddescription/para'):
        description = paragraph.text or ''
        for child in paragraph:
            if child.tag not in ('simplesect', 'parameterlist'):
                description += text(child)
            description += child.tail or ''
        if description.strip():
            paragraphs.append(description.strip())
    return paragraphs


def parameter_table(member):
    items = member.findall('.//parameterlist[@kind="param"]/parameteritem')
    if not items:
        return []
    rows = ['| 参数 | 方向 | 说明 |', '|---|---|---|']
    for item in items:
        name = item.find('parameternamelist/parametername')
        description = text(item.find('parameterdescription')).replace('\n', ' ')
        rows.append(f'| `{text(name)}` | {name.get("direction", "")} | {description} |')
    return rows


def generate(xml, output, root, platform="Windows x64", edition=None):
    product = json.loads((root / 'sdk/product.json').read_text(encoding='utf-8'))
    tree = ET.parse(xml)
    members = tree.findall('.//memberdef[@kind="function"]')
    names = [text(member.find('name')) for member in members]
    alloc_names = {name for name in names if name.endswith('Alloc') and name[:-5] in names}
    buffer_names = {name[:-5] for name in alloc_names}

    def function_title(name):
        if name in buffer_names:
            return f'{name} (buffer)'
        if name in alloc_names:
            return f'{name} (Alloc)'
        return name

    index = ET.parse(xml.with_name('index.xml'))
    structures = []
    for entry in index.findall('./compound[@kind="struct"]'):
        definition = xml.with_name(entry.get('refid') + '.xml')
        structures.append(ET.parse(definition).find('compounddef'))

    lifecycle = (root / 'docs/sdk/lifecycle.md').read_text(encoding='utf-8')
    _, anchor, body = lifecycle.partition('\n@anchor sdk_connection_modes\n')
    if not anchor:
        raise ValueError('Missing sdk_connection_modes section in docs/sdk/lifecycle.md')
    connection_guide = body.strip()
    lines = ['# CXWL Depth Camera SDK API 参考', '',
             f"SDK {product['display_version']} ({product['sdk_version']}) / ABI v{product['abi_version']} · {platform} C/C++ · {edition or product['release_date']}。{len(members)} 个公开函数。", '',
             '相关的数值、配置和函数指针见 [枚举值](#枚举值)、[宏定义](#宏定义)和[回调类型](#回调类型)。', '',
             connection_guide, '',
             '## 结构体索引', '', '| 结构体 | 用途 |', '|---|---|']
    for structure in structures:
        name = text(structure.find('compoundname'))
        summary = text(structure.find('briefdescription'))
        lines.append(f'| [`{name}`](#{name.lower()}) | {summary} |')
    lines += ['', '## 接口索引', '', '| 函数 | 作用 |', '|---|---|']
    for m in members:
        name = text(m.find('name'))
        anchor = function_title(name).lower().replace(' (', '-').rstrip(')')
        lines.append(f'| [`{name}`](#{anchor}) | {text(m.find("briefdescription"))} |')
    for m in members:
        name = text(m.find('name'))
        lines += ['', f'## {function_title(name)}', '', text(m.find('briefdescription')), '', '```c',
                  text(m.find('definition')) + text(m.find('argsstring')) + ';', '```', '']
        lines += parameter_table(m)
        for section in m.findall('.//simplesect'):
            kind = section.get('kind')
            if kind in ('return', 'note', 'since'):
                label = {'return': '返回值', 'note': '说明', 'since': '引入版本'}[kind]
                lines += ['', f'**{label}** {text(section).replace(chr(10), " ")}']
        if name in buffer_names:
            for section in m.findall('.//xrefsect/xrefdescription'):
                lines += ['', f'**已弃用** {text(section)}']
    lines += ['', '## 结构体成员']
    for structure in structures:
        name = text(structure.find('compoundname'))
        lines += ['', f'### {name}', '', text(structure.find('briefdescription')), '']
        for paragraph in details(structure):
            lines += [paragraph, '']
        lines += ['| 成员 | 类型 | 说明 |', '|---|---|---|']
        for field in structure.findall('sectiondef/memberdef[@kind="variable"]'):
            name = text(field.find('name')) + text(field.find('argsstring'))
            type_name = text(field.find('type'))
            description = text(field.find('briefdescription')) or text(field.find('detaileddescription'))
            lines.append(f'| `{name}` | `{type_name}` | {" ".join(description.split())} |')

    lines += ['', '## 枚举值']
    enums = tree.findall('.//memberdef[@kind="enum"]')
    for enum in sorted(enums, key=lambda member: int(member.find('location').get('line'))):
        lines += ['', '### ' + text(enum.find('briefdescription')), '']
        for paragraph in details(enum):
            lines += [paragraph, '']
        lines += ['| 常量 | 取值 | 说明 |', '|---|---|---|']
        for entry in enum.findall('enumvalue'):
            name = text(entry.find('name'))
            value = text(entry.find('initializer')).lstrip('= ').strip()
            description = text(entry.find('briefdescription')) or text(entry.find('detaileddescription'))
            lines.append(f'| `{name}` | `{value}` | {" ".join(description.split())} |')

    lines += ['', '## 宏定义', '', '| 宏 | 定义 | 用途 |', '|---|---|---|']
    version_header = ET.parse(xml.with_name('_depth_sdk_version_8h.xml'))
    for macro in tree.findall('.//memberdef[@kind="define"]') + version_header.findall('.//memberdef[@kind="define"]'):
        name = text(macro.find('name'))
        value = text(macro.find('initializer'))
        description = ''.join([text(macro.find('briefdescription')), *details(macro)]).strip()
        description = re.sub(r'(__declspec\(\w+\))', r'`\1`', description)
        lines.append(f'| `{name}` | `{value}` | {" ".join(description.split())} |')

    lines += ['', '## 回调类型']
    header = (root / 'sdk/DepthSdk.h').read_text(encoding='utf-8').splitlines()
    typedefs = tree.findall('.//memberdef[@kind="typedef"]')
    callbacks = [item for item in typedefs if text(item.find('name')).endswith('Callback')]
    for callback in sorted(callbacks, key=lambda member: int(member.find('location').get('line'))):
        name = text(callback.find('name'))
        start = int(callback.find('location').get('line')) - 1
        # The XML definition omits the calling-convention macro.
        declaration = '\n'.join(header[start:]).partition(';')[0].strip() + ';'
        lines += ['', f'### {name}', '', text(callback.find('briefdescription')),
                  '', '```c', declaration, '```', '']
        lines += parameter_table(callback)
        for note in callback.findall('.//simplesect[@kind="note"]'):
            lines += ['', text(note)]

    for heading, file in [('最小采集示例', 'sdk/DepthSdkExample.c'), ('复制深度与测量质量', 'sdk/examples/DepthQuality.c')]:
        lines += ['', '## ' + heading, '', '包含目录为 `SDK/include` 和 `Example/common`，链接 CMake 目标 `CXWL::DepthSdk`。`sample_poll()` 每轮在所有者线程调用 `CxwlDepth_WaitForEvent`。', '', '```c', (root / file).read_text(encoding='utf-8').rstrip(), '```']
    for page in ['terminology', 'data', 'specifications', 'json', 'errors', 'examples', 'build', 'usb', 'rgb_lsc']:
        content = (root / f'docs/sdk/{page}.md').read_text(encoding='utf-8')
        # The combined manual nests each tutorial's sections under its page title.
        content = re.sub(r'^(#{2,5}) ', r'#\1 ', content, flags=re.M)
        content = re.sub(r'^@page \S+ (.*)', r'## \1', content)
        content = re.sub(r'^@anchor \w+\s*$', '', content, flags=re.M)
        content = content.replace(r'\@', '@')
        content = content.replace('@ref sdk_validation_scope "版本兼容矩阵与验收范围"',
                                  '[版本兼容矩阵与验收范围](#版本兼容矩阵与验收范围)')
        content = content.replace('@ref sdk_rgb_lsc "双绿矫正网格"',
                                  '[双绿矫正网格](#sc3336-双绿矫正网格)')
        content = content.replace('@ref sdk_json "JSON 返回契约"', '[JSON 返回契约](#json-返回契约)')
        for target, title in [('sdk_usb', 'USB 登记、网络与凭据'), ('sdk_build', '构建、部署与 Qt 宿主'),
                              ('sdk_lifecycle', '选择连接方式')]:
            content = re.sub(r'@ref ' + target + r'\b', f'[{title}]({target}.html)', content)
        content = content.replace('@include DepthQuality.c', '质量图示例见[复制深度与测量质量](#复制深度与测量质量)。')
        content = content.replace('@verbinclude samples.json',
            chr(96)*3 + 'json\n' + (root / 'docs/sdk/schemas/samples.json').read_text(encoding='utf-8').rstrip() + '\n' + chr(96)*3)
        lines += ['', content.rstrip()]
    output.write_text('\n'.join(lines) + '\n', encoding='utf-8')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--xml', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--edition', help='Identify a preview using the label supplied by its build.')
    args = p.parse_args()
    generate(args.xml, args.output, Path(__file__).resolve().parents[1], edition=args.edition)
