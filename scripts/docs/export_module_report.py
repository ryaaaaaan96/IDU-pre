#!/usr/bin/env python3
"""将正式模块报告导出为 Word；仅使用 Python 标准库。"""

import argparse
from html.parser import HTMLParser
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile
import xml.etree.ElementTree as ET

W = "http://schemas.openxmlformats.org/wordprocessingml/2006/main"
R = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
ET.register_namespace("w", W)
ET.register_namespace("r", R)


class Node:
    def __init__(self, tag, attrs=()):
        self.tag = tag
        self.attrs = dict(attrs)
        self.children = []

    def text(self):
        return "".join(c if isinstance(c, str) else c.text() for c in self.children)


class DocumentParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.root = Node("root")
        self.stack = [self.root]

    def handle_starttag(self, tag, attrs):
        node = Node(tag, attrs)
        self.stack[-1].children.append(node)
        if tag not in {"meta", "br", "hr", "img", "input", "link"}:
            self.stack.append(node)

    def handle_endtag(self, tag):
        if self.stack[-1].tag != tag:
            raise ValueError(f"Unbalanced HTML: {tag}")
        self.stack.pop()

    def handle_data(self, text):
        self.stack[-1].children.append(text)


def element(parent, name, attrs=None):
    return ET.SubElement(
        parent,
        f"{{{W}}}{name}",
        {f"{{{W}}}{key}": str(value) for key, value in (attrs or {}).items()},
    )


def paragraph(parent, text="", style=None, align=None):
    p = element(parent, "p")
    props = element(p, "pPr")
    if style:
        element(props, "pStyle", {"val": style})
    if align:
        element(props, "jc", {"val": align})
    run = element(p, "r")
    for index, line in enumerate(text.split("\n")):
        if index:
            element(run, "br")
        t = element(run, "t")
        t.set("{http://www.w3.org/XML/1998/namespace}space", "preserve")
        t.text = line
    return p


def descendants(node, tag):
    found = [node] if node.tag == tag else []
    for child in node.children:
        if isinstance(child, Node):
            found.extend(descendants(child, tag))
    return found


def normalized(node):
    return " ".join(node.text().split())


def table(parent, node):
    tbl = element(parent, "tbl")
    props = element(tbl, "tblPr")
    element(props, "tblW", {"w": 0, "type": "auto"})
    element(props, "tblLayout", {"type": "fixed"})
    borders = element(props, "tblBorders")
    for edge in ("top", "left", "bottom", "right", "insideH", "insideV"):
        element(borders, edge, {"val": "single", "sz": 4, "color": "999999"})
    rows = descendants(node, "tr")
    cols = max(len([c for c in row.children if isinstance(c, Node)]) for row in rows)
    grid = element(tbl, "tblGrid")
    for _ in range(cols):
        element(grid, "gridCol", {"w": 9360 // cols})
    for row in rows:
        tr = element(tbl, "tr")
        cells = [c for c in row.children if isinstance(c, Node) and c.tag in {"td", "th"}]
        if any(c.tag == "th" for c in cells):
            element(element(tr, "trPr"), "tblHeader")
        for cell in cells:
            tc = element(tr, "tc")
            tcpr = element(tc, "tcPr")
            element(tcpr, "tcW", {"w": 9360 // cols, "type": "dxa"})
            if cell.tag == "th":
                element(tcpr, "shd", {"fill": "E8EDF2"})
            paragraph(tc, normalized(cell), "TableText")
    paragraph(parent)


def render(parent, node):
    if node.tag in {"head", "style", "script", "nav", "button"}:
        return
    if node.attrs.get("class") == "cover":
        for child in node.children:
            if isinstance(child, Node):
                style = "Title" if child.tag == "h1" else "Subtitle"
                paragraph(parent, normalized(child), style, "center")
        element(element(paragraph(parent), "r"), "br", {"type": "page"})
        return
    if node.tag in {"h1", "h2", "h3", "h4"}:
        style = {"h1": "Title", "h2": "Heading1", "h3": "Heading2", "h4": "Heading3"}
        paragraph(parent, normalized(node), style[node.tag])
        return
    if node.tag == "pre":
        # 每行独立成段，允许长代码跨页，同时保留缩进。
        for line in node.text().splitlines():
            paragraph(parent, line, "Code")
        paragraph(parent)
        return
    if node.tag == "table":
        table(parent, node)
        return
    if node.tag in {"p", "li", "dt", "dd"}:
        prefix = "• " if node.tag == "li" else ""
        paragraph(parent, prefix + normalized(node))
        return
    if node.attrs.get("class") in {"flow", "chips", "meta"}:
        text = " ".join(normalized(c) for c in node.children if isinstance(c, Node))
        paragraph(parent, text)
        return
    for child in node.children:
        if isinstance(child, Node):
            render(parent, child)
        elif child.strip():
            paragraph(parent, " ".join(child.split()))


def xml(root):
    return ET.tostring(root, encoding="utf-8", xml_declaration=True)


def styles():
    root = ET.Element(f"{{{W}}}styles")
    defaults = element(root, "docDefaults")
    rp = element(element(defaults, "rPrDefault"), "rPr")
    element(rp, "rFonts", {"ascii": "Calibri", "hAnsi": "Calibri", "eastAsia": "宋体"})
    element(rp, "sz", {"val": 22})
    element(rp, "lang", {"val": "zh-CN", "eastAsia": "zh-CN"})
    for name, size in [
        ("Normal", 22),
        ("Title", 40),
        ("Subtitle", 24),
        ("Heading1", 30),
        ("Heading2", 26),
        ("Heading3", 23),
        ("Code", 17),
        ("TableText", 19),
    ]:
        style = element(root, "style", {"type": "paragraph", "styleId": name})
        element(style, "name", {"val": name})
        if name != "Normal":
            element(style, "basedOn", {"val": "Normal"})
        pp = element(style, "pPr")
        element(pp, "spacing", {"after": 120, "line": 320, "lineRule": "auto"})
        if name.startswith("Heading"):
            element(pp, "keepNext")
            element(pp, "outlineLvl", {"val": int(name[-1]) - 1})
            element(pp, "spacing", {"before": 240, "after": 140})
        rp = element(style, "rPr")
        element(rp, "sz", {"val": size})
        if name.startswith("Heading") or name == "Title":
            element(rp, "b")
            element(rp, "rFonts", {"eastAsia": "黑体"})
        if name == "Code":
            element(rp, "rFonts", {"ascii": "Consolas", "hAnsi": "Consolas", "eastAsia": "宋体"})
            element(pp, "spacing", {"after": 0, "line": 240, "lineRule": "auto"})
            element(pp, "shd", {"fill": "F4F4F4"})
    return root


def export(source, target):
    parser = DocumentParser()
    parser.feed(source.read_text())
    if len(parser.stack) != 1:
        raise ValueError("Unclosed HTML")
    document = ET.Element(f"{{{W}}}document")
    body = element(document, "body")
    main = descendants(parser.root, "main")[0]
    render(body, main)
    section = element(body, "sectPr")
    ET.SubElement(
        section, f"{{{W}}}headerReference", {f"{{{W}}}type": "default", f"{{{R}}}id": "rId2"}
    )
    ET.SubElement(
        section, f"{{{W}}}footerReference", {f"{{{W}}}type": "default", f"{{{R}}}id": "rId3"}
    )
    element(section, "pgSz", {"w": 11906, "h": 16838})
    element(
        section,
        "pgMar",
        {"top": 1417, "bottom": 1417, "left": 1276, "right": 1276, "header": 708, "footer": 708},
    )
    header = ET.Element(f"{{{W}}}hdr")
    paragraph(header, "IDU-pre  |  Zephyr 通用模块设计报告", "TableText")
    footer = ET.Element(f"{{{W}}}ftr")
    p = paragraph(footer, "", align="center")
    field = element(p, "fldSimple", {"instr": "PAGE"})
    element(element(field, "r"), "t").text = "1"
    package_rel = "http://schemas.openxmlformats.org/package/2006/relationships"
    rels = ET.Element("Relationships", {"xmlns": package_rel})
    for ident, kind, name in [
        ("rId1", "styles", "styles.xml"),
        ("rId2", "header", "header1.xml"),
        ("rId3", "footer", "footer1.xml"),
    ]:
        ET.SubElement(rels, "Relationship", {"Id": ident, "Type": R + "/" + kind, "Target": name})
    types = ET.Element(
        "Types", {"xmlns": "http://schemas.openxmlformats.org/package/2006/content-types"}
    )
    ET.SubElement(
        types,
        "Default",
        {
            "Extension": "rels",
            "ContentType": "application/vnd.openxmlformats-package.relationships+xml",
        },
    )
    ET.SubElement(types, "Default", {"Extension": "xml", "ContentType": "application/xml"})
    for name, kind in [
        ("document", "document.main"),
        ("styles", "styles"),
        ("header1", "header"),
        ("footer1", "footer"),
    ]:
        ET.SubElement(
            types,
            "Override",
            {
                "PartName": "/word/" + name + ".xml",
                "ContentType": "application/vnd.openxmlformats-officedocument.wordprocessingml."
                + kind
                + "+xml",
            },
        )
    root_rels = ET.Element("Relationships", {"xmlns": package_rel})
    ET.SubElement(
        root_rels,
        "Relationship",
        {"Id": "rId1", "Type": R + "/officeDocument", "Target": "word/document.xml"},
    )
    target.parent.mkdir(parents=True, exist_ok=True)
    with ZipFile(target, "w", ZIP_DEFLATED) as archive:
        for name, data in [
            ("[Content_Types].xml", xml(types)),
            ("_rels/.rels", xml(root_rels)),
            ("word/document.xml", xml(document)),
            ("word/styles.xml", xml(styles())),
            ("word/_rels/document.xml.rels", xml(rels)),
            ("word/header1.xml", xml(header)),
            ("word/footer1.xml", xml(footer)),
        ]:
            archive.writestr(name, data)
    print(f"Created {target}")


if __name__ == "__main__":
    repo = Path(__file__).resolve().parents[2]
    cli = argparse.ArgumentParser(description="Export the module design report to Word")
    cli.add_argument("--source", type=Path, default=repo / "docs/module-design-report.html")
    cli.add_argument("--output", type=Path, default=repo / "docs/module-design-report.docx")
    args = cli.parse_args()
    export(args.source, args.output)
