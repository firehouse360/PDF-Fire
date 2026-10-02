#!/usr/bin/env python3
"""PDF Fire test corpus generator: hand-written minimal PDFs, no third-party libraries."""
import os, zlib

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "corpus")


def build(objects, with_id=True):
    """objects: list of bytes bodies, object numbers 1..n. Object 1 must be the catalog."""
    out = bytearray(b"%PDF-1.7\n%\xe2\xe3\xcf\xd3\n")
    offsets = []
    for i, body in enumerate(objects, 1):
        offsets.append(len(out))
        out += b"%d 0 obj\n" % i + body + b"\nendobj\n"
    xref = len(out)
    out += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objects) + 1)
    for off in offsets:
        out += b"%010d 00000 n \n" % off
    ident = b" /ID [<00112233445566778899aabbccddeeff> <00112233445566778899aabbccddeeff>]" if with_id else b""
    out += b"trailer\n<< /Size %d /Root 1 0 R%s >>\n" % (len(objects) + 1, ident)
    out += b"startxref\n%d\n%%%%EOF\n" % xref
    return bytes(out)


def stream(dictionary, data):
    return b"<< " + dictionary + b" /Length %d >>\nstream\n" % len(data) + data + b"\nendstream"


def image_and_text(redact_marks=False, overlapping_quads=False, origin=0, with_id=True):
    """One 400x400 page: a 100x100 RGB image filling (50,200)-(350,390) and one line of text.
    Image: left half pure red (255,0,0), right half pure green (0,255,0). A redaction over the
    left half must leave NO pure-red pixel in the output file."""
    w = h = 100
    rows = bytearray()
    for _ in range(h):
        rows += b"\xff\x00\x00" * (w // 2) + b"\x00\xff\x00" * (w // 2)
    img = stream(b"/Type /XObject /Subtype /Image /Width 100 /Height 100 /ColorSpace /DeviceRGB "
                 b"/BitsPerComponent 8 /Filter /FlateDecode", zlib.compress(bytes(rows)))
    content = (b"q 300 0 0 190 50 200 cm /Im1 Do Q\n"
               b"BT /F1 24 Tf 50 100 Td (TOPSECRET 123-45-6789) Tj ET\n"
               b"BT /F1 12 Tf 50 60 Td (public footer text) Tj ET\n")
    # Redact marks: the red (left) half of the image, and the TOPSECRET line. The footer stays.
    annots = b" /Annots [7 0 R 8 0 R]" if redact_marks else b""
    extra = [b"<< /Type /Annot /Subtype /Redact /Rect [40 190 200 395] /IC [0 0 0] >>",
             b"<< /Type /Annot /Subtype /Redact /Rect [40 90 395 130] /IC [0 0 0] >>"] if redact_marks else []
    if overlapping_quads:
        # ONE annotation with two quadrilaterals that overlap in the strip y = 280..300 (like the
        # boxes of two adjacent text lines). With an odd-even fill the strip would stay unredacted.
        extra[0] = (b"<< /Type /Annot /Subtype /Redact /Rect [40 190 200 395] /IC [0 0 0] /QuadPoints "
                    b"[40 300 200 300 40 190 200 190  40 395 200 395 40 280 200 280] >>")
    if origin:
        # Same page, but its media box does not start at the origin: everything is moved by
        # (origin, origin), so the page must look exactly the same.
        def shift(nums):
            return b" ".join(b"%d" % (int(n) + origin) for n in nums.split())
        content = (b"q 300 0 0 190 %d %d cm /Im1 Do Q\n" % (50 + origin, 200 + origin) +
                   b"BT /F1 24 Tf %d %d Td (TOPSECRET 123-45-6789) Tj ET\n" % (50 + origin, 100 + origin) +
                   b"BT /F1 12 Tf %d %d Td (public footer text) Tj ET\n" % (50 + origin, 60 + origin))
        extra = [b"<< /Type /Annot /Subtype /Redact /Rect [" + shift(b"40 190 200 395") + b"] /IC [0 0 0] >>",
                 b"<< /Type /Annot /Subtype /Redact /Rect [" + shift(b"40 90 395 130") + b"] /IC [0 0 0] >>"]
    media_box = b"[%d %d %d %d]" % (origin, origin, 400 + origin, 400 + origin)
    return build([
        b"<< /Type /Catalog /Pages 2 0 R >>",
        b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        b"<< /Type /Page /Parent 2 0 R /MediaBox " + media_box + b" /Contents 4 0 R "
        b"/Resources << /Font << /F1 5 0 R >> /XObject << /Im1 6 0 R >> >>" + annots + b" >>",
        stream(b"", content),
        b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>",
        img,
    ] + extra, with_id=with_id)


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "image_and_text.pdf"), "wb") as f:
        f.write(image_and_text())
    with open(os.path.join(OUT, "redact_marked.pdf"), "wb") as f:
        f.write(image_and_text(redact_marks=True))
    with open(os.path.join(OUT, "redact_overlap.pdf"), "wb") as f:
        f.write(image_and_text(redact_marks=True, overlapping_quads=True))
    with open(os.path.join(OUT, "redact_offset_mediabox.pdf"), "wb") as f:
        f.write(image_and_text(redact_marks=True, origin=150))
    with open(os.path.join(OUT, "no_document_id.pdf"), "wb") as f:
        f.write(image_and_text(with_id=False))
    print("wrote", os.listdir(OUT))
