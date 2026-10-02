#!/usr/bin/env python3
"""PDF Fire end-to-end regression tests. Drives PdfTool and cross-checks the output with
independent tools (qpdf, poppler's pdftotext/pdfimages) so we never grade our own homework.

Usage: tests/regress.py [build kind, default: release]
Needs: qpdf, poppler-utils, python3-pil.
"""
import os, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KIND = sys.argv[1] if len(sys.argv) > 1 else "release"
PDFTOOL = os.path.join(ROOT, "build", KIND, "bin", "PdfTool")
CORPUS = os.path.join(ROOT, "tests", "corpus")
ENV = dict(os.environ, QT_QPA_PLATFORM="offscreen")
SECRET = "TOPSECRET 123-45-6789"

failures = []


def run(*args):
    return subprocess.run(args, env=ENV, capture_output=True, text=True, errors="replace")


def check(name, condition, detail=""):
    print(("  ok   " if condition else "  FAIL ") + name + ("" if condition else "  -- " + detail))
    if not condition:
        failures.append(name)


def first_line(text):
    return text.splitlines()[0].strip() if text.strip() else ""


def test_redaction_erases_image_pixels(tmp, source="redact_marked.pdf"):
    """The left (red) half of the image is under a redact annotation. No red pixel may survive
    in the image stored in the output file, and the text under the second mark must be gone."""
    from PIL import Image
    out = os.path.join(tmp, "redacted.pdf")
    r = run(PDFTOOL, "redact", os.path.join(CORPUS, source), out)
    check("redact: tool succeeds", r.returncode == 0 and os.path.exists(out), r.stdout + r.stderr)
    if not os.path.exists(out):
        return
    run("pdfimages", "-png", out, os.path.join(tmp, "img"))
    images = sorted(f for f in os.listdir(tmp) if f.startswith("img-"))
    check("redact: output still contains the image", len(images) >= 1)
    red = green = 0
    for f in images:
        im = Image.open(os.path.join(tmp, f)).convert("RGB")
        for r_, g_, b_ in (im.getpixel((x, y)) for y in range(im.height) for x in range(im.width)):
            if r_ > 150 and g_ < 100 and b_ < 100:
                red += 1
            if g_ > 200 and r_ < 60 and b_ < 60:
                green += 1
    check("redact: no redacted pixel survives in the image", red == 0, f"{red} red pixels left")
    check("redact: pixels outside the mark are kept", green > 4000, f"only {green} green pixels")
    text = run("pdftotext", out, "-").stdout
    check("redact: redacted text is not extractable", "TOPSECRET" not in text and "6789" not in text)


def test_redaction_overlapping_quads(tmp):
    """Same, but the mark is one annotation made of two overlapping quadrilaterals."""
    test_redaction_erases_image_pixels(tmp, "redact_overlap.pdf")


def render(pdf, tmp, name):
    from PIL import Image
    run("pdftoppm", "-r", "72", "-png", "-singlefile", pdf, os.path.join(tmp, name))
    return Image.open(os.path.join(tmp, name + ".png")).convert("RGB")


def test_redaction_offset_mediabox(tmp):
    """A page whose media box does not start at the origin must be redacted at the same place
    as the same page at the origin: compare the rendered output of both."""
    test_redaction_erases_image_pixels(tmp, "redact_offset_mediabox.pdf")
    ref_pdf = os.path.join(tmp, "ref.pdf")
    run(PDFTOOL, "redact", os.path.join(CORPUS, "redact_marked.pdf"), ref_pdf)
    ref = render(ref_pdf, tmp, "ref")
    out = render(os.path.join(tmp, "redacted.pdf"), tmp, "out")
    check("redact (offset media box): page size is kept", ref.size == out.size, f"{ref.size} vs {out.size}")
    if ref.size != out.size:
        return
    probes = {"green half of the image": (275, 105), "black box over the image": (120, 105),
              "black box over the text": (200, 290), "white margin": (380, 380)}
    for name, xy in probes.items():
        a, b = ref.getpixel(xy), out.getpixel(xy)
        check(f"redact (offset media box): {name} is at the same place",
              max(abs(a[i] - b[i]) for i in range(3)) < 40, f"{a} vs {b} at {xy}")
    check("redact (offset media box): probes are meaningful", ref.getpixel((275, 105))[1] > 200 and sum(ref.getpixel((120, 105))) < 60,
          f"{ref.getpixel((275, 105))} {ref.getpixel((120, 105))}")


def test_encryption(tmp, rounds=12):
    """Every algorithm, with and without a separate owner password. The keys and salts are
    random, so the round trip is repeated: a binary string written wrongly only breaks the
    file for some random values."""
    src = os.path.join(CORPUS, "image_and_text.pdf")
    bad = {"qpdf-user": 0, "poppler-user": 0, "owner": 0, "opens-without-password": 0}
    total = 0
    for _ in range(rounds):
        for alg in ("rc4", "aes-128", "aes-256"):
            for owner in ("", "ownerpw"):
                total += 1
                f = os.path.join(tmp, "enc.pdf")
                shutil.copy(src, f)
                args = [PDFTOOL, "encrypt", "--enc-algorithm", alg, "--enc-user-password", "userpw"]
                if owner:
                    args += ["--enc-owner-password", owner]
                run(*args, f)
                if run("qpdf", "--password=userpw", "--check", f).returncode != 0:
                    bad["qpdf-user"] += 1
                if first_line(run("pdftotext", "-upw", "userpw", f, "-").stdout) != SECRET:
                    bad["poppler-user"] += 1
                if owner and first_line(run("pdftotext", "-opw", owner, f, "-").stdout) != SECRET:
                    bad["owner"] += 1
                if run("qpdf", "--check", f).returncode == 0 or SECRET in run("pdftotext", f, "-").stdout:
                    bad["opens-without-password"] += 1
    check(f"encrypt: qpdf opens all {total} files with the user password", bad["qpdf-user"] == 0, str(bad))
    check(f"encrypt: poppler opens all {total} files with the user password", bad["poppler-user"] == 0, str(bad))
    check("encrypt: owner password opens the file", bad["owner"] == 0, str(bad))
    check("encrypt: NO file opens without a password", bad["opens-without-password"] == 0, str(bad))

    f = os.path.join(tmp, "nopw.pdf")
    shutil.copy(src, f)
    r = run(PDFTOOL, "encrypt", "--enc-algorithm", "aes-256", f)
    check("encrypt: refuses when both passwords are empty", r.returncode != 0)
    check("encrypt: a refused file is left untouched", open(f, "rb").read() == open(src, "rb").read())

    f = os.path.join(tmp, "owneronly.pdf")
    shutil.copy(src, f)
    run(PDFTOOL, "encrypt", "--enc-algorithm", "aes-256", "--enc-owner-password", "ownerpw", f)
    check("encrypt: owner-only (permissions) file opens for reading", SECRET in run("pdftotext", f, "-").stdout)


def test_encryption_creates_document_id(tmp):
    """A document without the /ID array must get one when it is encrypted (ISO 32000 requires
    it), and must still open with its password in every algorithm."""
    for alg in ("rc4", "aes-128", "aes-256"):
        f = os.path.join(tmp, f"noid-{alg}.pdf")
        shutil.copy(os.path.join(CORPUS, "no_document_id.pdf"), f)
        run(PDFTOOL, "encrypt", "--enc-algorithm", alg, "--enc-user-password", "userpw", f)
        data = open(f, "rb").read()
        trailer = data[data.rfind(b"trailer"):]
        check(f"encrypt without /ID ({alg}): the output has an /ID", b"/ID" in trailer, trailer[:200].decode("latin1"))
        check(f"encrypt without /ID ({alg}): qpdf opens it", run("qpdf", "--password=userpw", "--check", f).returncode == 0)
        check(f"encrypt without /ID ({alg}): poppler opens it",
              first_line(run("pdftotext", "-upw", "userpw", f, "-").stdout) == SECRET)


def test_plain_write_is_valid(tmp):
    """A rewritten file must have a trailer /Size that matches its cross-reference table."""
    f = os.path.join(tmp, "plain.pdf")
    shutil.copy(os.path.join(CORPUS, "image_and_text.pdf"), f)
    run(PDFTOOL, "encrypt", "--enc-algorithm", "aes-256", "--enc-user-password", "u", f)
    r = run("qpdf", "--password=u", "--check", f)
    check("write: qpdf reports no structural warnings", "WARNING" not in r.stdout + r.stderr, r.stdout + r.stderr)


if __name__ == "__main__":
    if not os.path.exists(PDFTOOL):
        sys.exit(f"{PDFTOOL} not found - build first (scripts/build.sh {KIND})")
    subprocess.run([sys.executable, os.path.join(ROOT, "tests", "make_corpus.py")], stdout=subprocess.DEVNULL, check=True)
    with tempfile.TemporaryDirectory(prefix="pdffire-regress-") as tmp:
        for test in (test_redaction_erases_image_pixels, test_redaction_overlapping_quads, test_redaction_offset_mediabox, test_encryption, test_encryption_creates_document_id, test_plain_write_is_valid):
            print(test.__name__)
            sub = os.path.join(tmp, test.__name__)
            os.makedirs(sub)
            test(sub)
    print(f"\n{'FAILED: ' + ', '.join(failures) if failures else 'all regression checks passed'}")
    sys.exit(1 if failures else 0)
