"""Render the generated SDK Markdown as a versioned, searchable offline PDF.

Requires reportlab. Usage: python build_sdk_pdf.py --markdown API_REFERENCE.md
--output API_REFERENCE.pdf. The Markdown is the generated source, not a second manual.
"""
import argparse
import html
import json
import re
from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (BaseDocTemplate, Frame, PageTemplate, Paragraph,
                               Spacer, PageBreak, LongTable, TableStyle, Image, Preformatted)
from reportlab.platypus.tableofcontents import TableOfContents


def inline(value):
    value = value.replace('\\|', '|').replace('\\@', '@')
    value = html.escape(value)
    value = re.sub(r'`([^`]+)`', r'<font color="#155e75">\1</font>', value)
    value = re.sub(r'\*\*([^*]+)\*\*', r'<b>\1</b>', value)
    value = re.sub(r'\[([^]]+)\]\(([^)]+)\)', r'\1', value)
    return value


class Manual(BaseDocTemplate):
    def afterFlowable(self, flowable):
        if isinstance(flowable, Paragraph) and flowable.style.name in ('H1', 'H2'):
            level = int(flowable.style.name[-1]) - 1
            title = flowable.getPlainText()
            key = f'heading-{self.seq.nextf("heading")}'
            self.canv.bookmarkPage(key)
            self.canv.addOutlineEntry(title, key, level, closed=True)
            self.notify('TOCEntry', (level, title, self.page, key))


def build(markdown, output, font, bold_font, platform="Windows x64", handout=False, edition=None):
    product = json.loads((Path(__file__).resolve().parents[1] / 'sdk/product.json').read_text(encoding='utf-8'))
    lines = markdown.read_text(encoding='utf-8').splitlines()
    function_count = sum(line.startswith('## CxwlDepth_') for line in lines)
    pdfmetrics.registerFont(TTFont('Body', str(font), subfontIndex=0))
    pdfmetrics.registerFont(TTFont('BodyBold', str(bold_font), subfontIndex=0))
    pdfmetrics.registerFontFamily('Body', normal='Body', bold='BodyBold', italic='Body', boldItalic='BodyBold')
    width, height = A4
    usable = width - 88
    base = dict(fontName='Body', fontSize=8.5, leading=13.5, wordWrap='CJK', spaceAfter=6)
    body = ParagraphStyle('BodyText', **base)
    code = ParagraphStyle('Code', fontName='Body', fontSize=7.1, leading=10, wordWrap='CJK',
                          backColor=colors.HexColor('#f3f6f9'), borderPadding=3, spaceAfter=0)
    cell = ParagraphStyle('Cell', fontName='Body', fontSize=7.2, leading=10.5, wordWrap='CJK')
    h1 = ParagraphStyle('H1', fontName='BodyBold', fontSize=16, leading=23, spaceBefore=16,
                        spaceAfter=10, textColor=colors.HexColor('#124466'), keepWithNext=True, wordWrap='CJK')
    h2 = ParagraphStyle('H2', parent=h1, fontSize=11, leading=17, spaceBefore=10, spaceAfter=7)
    title = ParagraphStyle('Title', parent=h1, fontSize=26, leading=38, alignment=TA_CENTER)
    if handout:
        guide_title = lines[0].removeprefix('# ').strip()
        body.fontSize, body.leading, body.spaceAfter = 10.5, 17, 9
        cell.fontSize, cell.leading = 9, 15
        h1.fontSize, h1.leading, h1.spaceBefore = 15, 22, 11
        pdfmetrics.registerFont(TTFont('GuideMono', str(font.parent / 'consola.ttf')))
        code = ParagraphStyle('Reader', fontName='GuideMono', fontSize=10, leading=13.5,
                              leftIndent=12, spaceBefore=3, spaceAfter=13)
        doc = BaseDocTemplate(str(output), pagesize=A4, title=guide_title, author='', creator='ReportLab')
        def decorate_guide(canvas, document):
            canvas.saveState()
            canvas.setFont('Body', 9)
            canvas.setFillColor(colors.HexColor('#436579'))
            canvas.drawString(44, height-29, '深度相机  /  录制数据')
            canvas.setStrokeColor(colors.HexColor('#bbced7'))
            canvas.line(44, height-38, width-44, height-38)
            canvas.line(44, 37, width-44, 37)
            canvas.drawString(44, 22, guide_title)
            canvas.drawRightString(width-44, 22, str(document.page))
            canvas.restoreState()
        doc.addPageTemplates(PageTemplate('Guide', [Frame(44, 52, usable, height-107,
                             leftPadding=0, rightPadding=0, topPadding=0, bottomPadding=0)], onPage=decorate_guide))
        story = [Paragraph(guide_title, ParagraphStyle('GuideTitle', parent=h1, fontSize=25,
                           leading=34, spaceBefore=0, spaceAfter=12))]
    else:
        doc = Manual(str(output), pagesize=A4, leftMargin=44, rightMargin=44, topMargin=50, bottomMargin=42,
                     title=f'CXWL Depth SDK {product["sdk_version"]} API Reference', author='',
                     creator='SDK documentation (ReportLab)')
        def decorate(canvas, document):
            canvas.saveState()
            canvas.setFont('Body', 8)
            canvas.setFillColor(colors.HexColor('#526879'))
            canvas.drawString(44, height-28, 'CXWL 深度相机 | C/C++ SDK 开发手册')
            canvas.setStrokeColor(colors.HexColor('#d8e2eb'))
            canvas.line(44, 32, width-44, 32)
            canvas.drawString(44, 20, f'SDK {product["sdk_version"]} / ABI v{product["abi_version"]} | {edition or product["release_date"]}')
            canvas.drawRightString(width-44, 20, str(document.page))
            canvas.restoreState()
        doc.addPageTemplates(PageTemplate('Manual', [Frame(44, 42, usable, height-92, leftPadding=0,
                            rightPadding=0, topPadding=0, bottomPadding=0)], onPage=decorate))
        story = [Spacer(1, 100), Paragraph('CXWL 深度相机<br/>C/C++ SDK 开发手册', title), Spacer(1, 30),
                 Paragraph(f'SDK {product["sdk_version"]} / ABI v{product["abi_version"]}'
                           + (f'<br/>{edition}' if edition else ''),
                           ParagraphStyle('CoverVersion', parent=body, alignment=TA_CENTER, fontSize=14)),
                 Spacer(1, 40),
                 Paragraph(f'适用平台为 {platform}。包含接入流程、{function_count} 个接口、结构体、数据契约、部署、示例与排障。', body),
                 Paragraph('本手册由公开头文件与 SDK 教程自动生成，与同包 HTML／Markdown 使用同一正文。硬件数据的适用条件与出处见测量规格章节。', body),
                 PageBreak(), Paragraph('目录', title)]
        toc = TableOfContents()
        toc.levelStyles = [ParagraphStyle('TOC1', parent=body, fontSize=9, spaceBefore=4),
                           ParagraphStyle('TOC2', parent=body, leftIndent=12, fontSize=8, spaceBefore=1)]
        story.extend([toc, PageBreak()])
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        if not line or line.startswith('@anchor'):
            i += 1; continue
        if handout and line == '---':
            story.append(PageBreak()); i += 1; continue
        if line.startswith(('```', '~~~')):
            fence = line[:3]; i += 1
            if handout:
                start = i
                while i < len(lines) and not lines[i].startswith(fence):
                    i += 1
                story.append(Preformatted('\n'.join(lines[start:i]), code))
                i += 1; continue
            while i < len(lines) and not lines[i].startswith(fence):
                value = html.escape(lines[i]).replace(' ', '&#160;') or '&#160;'
                story.append(Paragraph(value, code)); i += 1
            story.append(Spacer(1, 8)); i += 1; continue
        heading = re.match(r'^(#{1,6})\s+(.*)', line)
        if heading:
            if len(heading[1]) != 1:
                story.append(Paragraph(inline(heading[2]), h1 if len(heading[1]) == 2 else h2))
            i += 1; continue
        picture = re.match(r'^!\[([^]]*)\]\(([^)]+)\)', line)
        if picture:
            asset = markdown.parent / picture[2]
            if asset.is_file():
                im = Image(str(asset)); im.drawHeight *= usable/im.drawWidth; im.drawWidth = usable
                story.extend([im, Paragraph(inline(picture[1]), body)])
            else: raise ValueError(f'Missing image: {asset}')
            i += 1; continue
        if line.startswith('|'):
            rows = []
            while i < len(lines) and lines[i].strip().startswith('|'):
                parts = re.split(r'(?<!\\)\|', lines[i].strip().strip('|'))
                if not all(re.fullmatch(r'\s*:?-+:?\s*', c) for c in parts):
                    rows.append([Paragraph(inline(c.strip()), cell) for c in parts])
                i += 1
            count = len(rows[0])
            fractions = {2:[.32,.68],3:[.27,.23,.5],4:[.2,.18,.18,.44],5:[.15,.2,.22,.28,.15]}
            sizes = fractions.get(count, [1/count]*count)
            table = LongTable(rows, colWidths=[usable*s for s in sizes], repeatRows=1, hAlign='LEFT')
            table.setStyle(TableStyle([
                ('BACKGROUND',(0,0),(-1,0),colors.HexColor('#e2ecf3')),
                ('ROWBACKGROUNDS',(0,1),(-1,-1),[colors.white,colors.HexColor('#f7f9fb')]),
                ('VALIGN',(0,0),(-1,-1),'TOP'),('GRID',(0,0),(-1,-1),.3,colors.HexColor('#d4dfe7')),
                ('LEFTPADDING',(0,0),(-1,-1),5),('RIGHTPADDING',(0,0),(-1,-1),5),
                ('TOPPADDING',(0,0),(-1,-1),5),('BOTTOMPADDING',(0,0),(-1,-1),5)]))
            story.extend([table, Spacer(1, 9)]); continue
        paragraph = [line]; i += 1
        while i < len(lines) and lines[i].strip() and not re.match(r'^(#|\||```|~~~|!\[|[-*] )', lines[i]):
            paragraph.append(lines[i].strip()); i += 1
        story.append(Paragraph(inline(' '.join(paragraph)), body))
    output.parent.mkdir(parents=True, exist_ok=True)
    doc.multiBuild(story)
    print(output)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--markdown', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--font', type=Path, default=Path('C:/Windows/Fonts/msyh.ttc'))
    p.add_argument('--bold-font', type=Path, default=Path('C:/Windows/Fonts/msyhbd.ttc'))
    p.add_argument('--handout', action='store_true', help='Render a standalone customer guide without the API cover or index.')
    p.add_argument('--edition', help='Identify a preview on its cover and page footers.')
    a = p.parse_args()
    build(a.markdown, a.output, a.font, a.bold_font, handout=a.handout, edition=a.edition)
