# PDF Fire

**A free PDF editor for Linux, built by [Firehouse 360](https://firehouse360.com) for fire departments, free for everyone.**
Edit, fill, sign and protect PDFs offline, without a subscription.

**Download:** https://firehouse360.com/tools/pdf-fire (signed `.deb` for Ubuntu/Debian and `.rpm` for Fedora,
with automatic updates). A signed Windows version is in the works.

PDF Fire is based on [PDF4QT](https://github.com/JakubMelka/PDF4QT) by Jakub Melka — the PDF engine and the
original applications are his work. The upstream README is kept in [README-PDF4QT.md](README-PDF4QT.md).
If PDF4QT is useful to you, please consider [sponsoring its author](https://github.com/sponsors/JakubMelka).

## What PDF Fire adds

- **Edit text in place** — click into a line or paragraph and type; the original font is kept.
- **Forms** — fill any form, or turn a printed form into a fillable one (detect fields, check boxes, signature lines).
- **Signatures** — certificate signatures with RFC 3161 timestamps, a sealed signing record, Certify (DocMDP),
  and department certificate authorities ([guide](pdf-fire/docs/department-certificates-guide.md)).
- **Permissions honored everywhere** — every tool obeys the document's security settings
  ([why other apps may not](pdf-fire/docs/station-pc-pdf-restrictions.md)).
- **Real redaction**, **OCR**, **scanning**, **Page Marks** (watermarks, headers/footers, Bates numbers, backgrounds).
- **Read aloud** with natural offline voices (Kokoro-82M via ONNX Runtime).
- **Review tools** — highlights, notes, stamps, circles and arrows; a ribbon interface.

## Building (Linux)

The helper scripts are in [`pdf-fire/scripts`](pdf-fire/scripts). They keep everything that is not source code —
`build/`, `deps/` (third-party libraries and the voice data), `dist/` and the signed repository — in a work
folder **outside** this repository: by default the parent folder of the checkout, or `PDFFIRE_WORK_DIR`.

```sh
pdf-fire/scripts/install-build-deps.sh   # Ubuntu packages (sudo)
pdf-fire/scripts/build-deps.sh           # third-party libraries into <work>/deps
pdf-fire/scripts/build.sh release        # build into <work>/build/release
pdf-fire/scripts/make-deb.sh             # Ubuntu/Debian package into <work>/dist
pdf-fire/scripts/make-rpm.sh             # Fedora package (podman) into <work>/dist
```

The read-aloud voices need ONNX Runtime 1.23.2 and the Kokoro-82M voice data in `<work>/deps/kokoro`
(see `Pdf4QtLibGui/CMakeLists.txt`). A script that downloads them, and automated Linux and Windows builds,
are being added.

## Licence

MIT — see [LICENSE](LICENSE). Third-party licences are in [3rdparty_licenses](3rdparty_licenses), including
the Kokoro voice model (Apache 2.0), misaki (Apache 2.0) and ONNX Runtime (MIT).

## Support

PDF Fire is free and stays free. If it helps you, you can
[support it](https://firehouse360.com/support/pdf-fire) — once or monthly, cancel anytime.
Questions: pdffire.constant740@passmail.net
