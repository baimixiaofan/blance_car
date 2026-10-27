# -*- coding: utf-8 -*-
"""Extract the embedded OLE object / WMF equation and dump P006 XML."""
import sys, io, re, zipfile, struct, os
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

PATH = r"C:\Users\baimixiaofan\Downloads\4.3.电机模型辨识测试作业.docx"
OUT = r"E:\control\car\scripts\_docx_extract"
os.makedirs(OUT, exist_ok=True)

with zipfile.ZipFile(PATH) as z:
    names = z.namelist()
    print("PARTS:")
    for n in names:
        print("  ", n, z.getinfo(n).file_size)

    data = {n: z.read(n) for n in names}

docxml = data["word/document.xml"].decode("utf-8", "replace")
i = docxml.find("OLEObject")
print()
print("=" * 70)
print("document.xml around OLEObject")
print("=" * 70)
if i != -1:
    print(docxml[max(0, i - 2500):i + 2500])
else:
    print("not found")

for n in ("word/media/image1.wmf", "word/embeddings/oleObject1.bin"):
    if n in data:
        p = os.path.join(OUT, os.path.basename(n))
        with open(p, "wb") as f:
            f.write(data[n])
        print(f"\nwrote {p} ({len(data[n])} bytes)")

# --- parse WMF text records ---
wmf = data.get("word/media/image1.wmf")
if wmf:
    print()
    print("=" * 70)
    print("WMF TEXT RECORDS")
    print("=" * 70)
    print("size", len(wmf), "header", wmf[:8].hex())
    off = 0
    # detect placeable header
    if wmf[:4] == b"\xd7\xcd\xc6\x9a":
        off = 22
    try:
        offtype, hsize, ver, size, nrec = struct.unpack_from("<HHHIH", wmf, off)
        print(f"metafile: type={offtype} hsize={hsize} ver={ver:#x} size={size} nrec={nrec}")
        off = off + hsize
        for _ in range(nrec or 0):
            if off + 6 > len(wmf):
                break
            rsize, rfunc = struct.unpack_from("<IH", wmf, off)
            if rsize < 6:
                break
            body = wmf[off + 6: off + rsize]
            if rfunc == 0x0A32 and len(body) >= 8:  # META_EXTTEXTOUT
                y, x, cnt, opts = struct.unpack_from("<hhHH", body, 0)
                s = body[8:8 + (cnt * 2)].decode("utf-16-le", "replace")
                print(f"  EXTTEXTOUT @({x},{y}) opts={opts:#x}: {s!r}")
            elif rfunc == 0x0521 and len(body) >= 2:  # META_TEXTOUT
                cnt = struct.unpack_from("<h", body, 0)[0]
                s = body[2:2 + cnt].decode("latin-1", "replace")
                print(f"  TEXTOUT: {s!r}")
            elif rfunc == 0x0324:  # POLYLINE
                pass
            off += rsize
    except Exception as e:
        print("parse error:", e)

# --- oleObject1.bin: look for printable text / MTEF ---
ole = data.get("word/embeddings/oleObject1.bin")
if ole:
    print()
    print("=" * 70)
    print("OLE OBJECT content")
    print("=" * 70)
    print("size", len(ole), "head", ole[:16].hex())
    u = ole.decode("latin-1", "replace")
    strs = re.findall(r"[ -~]{4,}", u)
    print("printable runs:", strs[:60])
    # MathType MTEF signature: 03 或者 'DSMT'
    for sig in (b"DSMT", b"Equation", b"MathType", b"MTEF"):
        idx = ole.find(sig)
        if idx != -1:
            print(f"found {sig!r} at {idx}")
    if b"MTEF" in ole or ole.find(b"DSMT") != -1:
        idx = ole.find(b"DSMT")
        if idx < 0:
            idx = ole.find(b"MTEF")
        print("around sig:", ole[max(0, idx - 40): idx + 300])
