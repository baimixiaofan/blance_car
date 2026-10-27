# -*- coding: utf-8 -*-
"""Dump OMML math, shapes, image refs and raw XML from the assignment docx."""
import sys, io, re
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

from docx import Document
from docx.oxml.ns import qn

PATH = r"C:\Users\baimixiaofan\Downloads\4.3.电机模型辨识测试作业.docx"
M = "{http://schemas.openxmlformats.org/officeDocument/2006/math}"

doc = Document(PATH)
body = doc.element.body
paras = body.findall(qn("w:p"))

print("=" * 70)
print("OMML MATH per paragraph")
print("=" * 70)
found = 0
for i, p in enumerate(paras):
    maths = p.findall(".//" + M + "oMath")
    if maths:
        found += 1
        texts = []
        for om in maths:
            ts = om.findall(".//" + M + "t")
            texts.append("".join(t.text or "" for t in ts))
        print(f"[P{i:03d}] MATH({len(maths)}): " + "  ||  ".join(texts))
print(f"paragraphs containing math: {found}")

print()
print("=" * 70)
print("PARAGRAPH ELEMENT COMPOSITION (non-w:r content)")
print("=" * 70)
for i, p in enumerate(paras):
    kids = set()
    for el in p.iter():
        t = el.tag.split("}")[-1]
        if t in ("oMath", "oMathPara", "drawing", "pict", "object", "shape", "txbxContent",
                 "imagedata", "blip", "AlternateContent", "p", "r", "pPr", "proofErr", "bookmarkStart"):
            continue
        kids.add(t)
    if kids:
        print(f"[P{i:03d}] {sorted(kids)}")

print()
print("=" * 70)
print("RAW XML of P016..P021")
print("=" * 70)
for i in range(16, 22):
    if i < len(paras):
        xml = paras[i].xml
        xml = re.sub(r'\s+xmlns:\w+="[^"]*"', "", xml)
        print(f"----- P{i:03d} -----")
        print(xml[:1500])
        print()

print("=" * 70)
print("ALL TEXT incl math per paragraph")
print("=" * 70)
for i, p in enumerate(paras):
    segs = [el.text for el in p.iter() if el.tag.split("}")[-1] == "t" and el.text]
    s = "".join(segs).strip()
    print(f"{i:03d}: {s if s else '<empty>'}")

print()
print("=" * 70)
print("DOCUMENT PARTS LIST")
print("=" * 70)
from docx.opc.constants import RELATIONSHIP_TYPE as RT
for rId, rel in doc.part.rels.items():
    print(f"{rId}: {rel.reltype.split('/')[-1]}  ->  {rel.target_ref}  external={rel.is_external}")
