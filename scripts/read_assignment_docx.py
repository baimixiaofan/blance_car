# -*- coding: utf-8 -*-
"""Extract full structure from the assignment docx."""
import sys
import io

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

from docx import Document
from docx.table import Table
from docx.text.paragraph import Paragraph
from docx.oxml.ns import qn

PATH = r"C:\Users\baimixiaofan\Downloads\4.3.电机模型辨识测试作业.docx"

doc = Document(PATH)

print("=" * 70)
print("CORE PROPERTIES")
print("=" * 70)
cp = doc.core_properties
for attr in ("title", "subject", "author", "last_modified_by", "created", "modified", "revision"):
    print(f"{attr}: {getattr(cp, attr, None)!r}")

print()
print("=" * 70)
print("SECTION / PAGE SETUP")
print("=" * 70)
for i, s in enumerate(doc.sections):
    print(f"section {i}: page {s.page_width}x{s.page_height}, margins L{s.left_margin} R{s.right_margin} T{s.top_margin} B{s.bottom_margin}")

body = doc.element.body


def iter_block_items(parent_element):
    for child in parent_element.iterchildren():
        if child.tag == qn("w:p"):
            yield Paragraph(child, doc)
        elif child.tag == qn("w:tbl"):
            yield Table(child, doc)


print()
print("=" * 70)
print("BODY (paragraphs + tables in document order)")
print("=" * 70)

p_idx = 0
t_idx = 0
for block in iter_block_items(body):
    if isinstance(block, Paragraph):
        text = block.text
        style = block.style.name if block.style is not None else "?"
        # detect numbered / bullet list formatting
        numpr = block._p.find(qn("w:pPr"))
        has_num = False
        ilvl = ""
        if numpr is not None:
            n = numpr.find(qn("w:numPr"))
            if n is not None:
                has_num = True
                il = n.find(qn("w:ilvl"))
                if il is not None:
                    ilvl = il.get(qn("w:val"))
        # inline images
        drawings = block._p.findall(".//" + qn("w:drawing"))
        pics = block._p.findall(".//" + qn("w:pict"))
        marker = ""
        if drawings or pics:
            marker = f" [IMAGE x{len(drawings) + len(pics)}]"
        if text.strip() or marker:
            tag = f"LIST(lvl={ilvl})" if has_num else ""
            print(f"[P{p_idx:03d}][{style}]{tag} {text}{marker}")
        else:
            print(f"[P{p_idx:03d}][{style}] <empty>")
        p_idx += 1
    else:
        rows = len(block.rows)
        cols = len(block.columns)
        print()
        print(f"--- TABLE {t_idx} ({rows} rows x {cols} cols) ---")
        for r, row in enumerate(block.rows):
            cells = []
            seen = set()
            for c, cell in enumerate(row.cells):
                # merged cells repeat; dedupe by id
                key = id(cell._tc)
                if key in seen:
                    continue
                seen.add(key)
                cells.append(cell.text.replace("\n", " / ").strip())
            print(f"  R{r}: " + " | ".join(cells))
        print(f"--- END TABLE {t_idx} ---")
        print()
        t_idx += 1

print()
print("=" * 70)
print("IMAGES / EMBEDDED PARTS")
print("=" * 70)
for rid, part in doc.part.related_parts.items():
    ct = getattr(part, "content_type", "")
    if ct.startswith("image"):
        print(f"{rid}: {ct}  size={len(part.blob)} bytes  name={getattr(part, 'partname', '')}")

print()
print("=" * 70)
print("HEADERS / FOOTERS")
print("=" * 70)
for i, s in enumerate(doc.sections):
    for name in ("header", "footer", "first_page_header", "first_page_footer", "even_page_header", "even_page_footer"):
        try:
            hf = getattr(s, name)
        except Exception:
            continue
        if hf is None:
            continue
        txt = " | ".join(p.text.strip() for p in hf.paragraphs if p.text.strip())
        if txt:
            print(f"section {i} {name}: {txt}")

print()
print("=" * 70)
print("SUMMARY: paragraphs=%d tables=%d images=%d" % (
    p_idx, t_idx, sum(1 for _, p in doc.part.related_parts.items() if getattr(p, "content_type", "").startswith("image"))))
print("=" * 70)
