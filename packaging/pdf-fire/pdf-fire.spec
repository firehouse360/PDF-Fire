# PDF Fire — Fedora package (binary repackaging).
#
# Not built from a tarball: scripts/make-rpm.sh compiles PDF Fire inside a Fedora container,
# against Fedora's own Qt and libraries, stages `cmake --install` under /opt/pdf-fire, and then
# runs rpmbuild on this spec in the same container with the stage passed in:
#
#   rpmbuild -bb pdf-fire.spec --define "pdffire_stage <dir>" --define "pdffire_version 0.1.0" \
#            --define "pdffire_engine 1.6.0.0" --define "pdffire_private_libs <regex>"
#
# Everything private lives in /opt/pdf-fire (the programs carry RPATH $ORIGIN/../lib), the same
# layout as the .deb from scripts/make-deb.sh, so nothing collides with a Fedora pdf4qt package.
# The package is left UNSIGNED on purpose — signing is a separate step.

%{!?pdffire_version: %global pdffire_version 0.1.0}
%{!?pdffire_engine: %global pdffire_engine 1.6.0.0}
%{!?pdffire_stage: %{error:pdffire_stage is not set - build with scripts/make-rpm.sh}}

# The stage is already stripped by make-rpm.sh; no debuginfo sub-packages
%global debug_package %{nil}
%global __strip /bin/true

# The private libraries in /opt/pdf-fire (the PDF4QT engine, its plugins, ONNX Runtime) are
# neither offered to the rest of the system nor required from it. Library dependencies on
# Fedora's own packages (Qt 6, OpenSSL, lcms2, tesseract, SANE ...) are still computed by rpm.
%global __provides_exclude_from ^/opt/pdf-fire/.*$
# (make-rpm.sh passes the exact sonames found in the stage; this default covers the same set)
%{!?pdffire_private_libs: %global pdffire_private_libs ^lib(Pdf4Qt[A-Za-z]*|onnxruntime[_a-z]*)[.]so.*$}
%global __requires_exclude %{pdffire_private_libs}

Name:           pdf-fire
Version:        %{pdffire_version}
Release:        1%{?dist}
Summary:        PDF editor - view, annotate, edit, sign, redact and protect PDF files

# PDF Fire and the PDF4QT engine are MIT. Bundled: ONNX Runtime (MIT), the Kokoro voice
# model (Apache-2.0), the misaki pronunciation data (Apache-2.0); the static Clipper2 (BSL-1.0),
# blend2d and asmjit (Zlib); the signature fonts in the resources (OFL-1.1, Apache-2.0).
# Their texts are in /usr/share/licenses/pdf-fire (src/3rdparty_licenses) and next to the
# voice data in /opt/pdf-fire/share/pdf-fire/voice.
License:        MIT AND Apache-2.0 AND BSL-1.0 AND Zlib AND OFL-1.1
URL:            https://github.com/JakubMelka/PDF4QT
ExclusiveArch:  x86_64

# ZapfDingbats clone (D050000L) and the rest of the base-35 fonts: without them form
# check boxes and the standard PDF fonts do not render
Requires:       urw-base35-fonts
Requires:       urw-base35-d050000l-fonts
Requires:       hicolor-icon-theme
# OCR (the OCR plugin links libtesseract; this adds the English data) and network scanners
Recommends:     tesseract
Recommends:     tesseract-langpack-eng
Recommends:     sane-airscan

%description
PDF Fire is a native PDF editor for Linux, built on the PDF4QT engine
(%{pdffire_engine}). Annotations, form filling, page content editing,
encryption and permissions, digital signatures, redaction, document compare,
page organising, OCR, scanning and natural-voice read aloud.

%prep
# nothing to unpack: the stage is a finished install tree

%build
# built by scripts/make-rpm.sh before rpmbuild runs

%install
cp -a %{pdffire_stage}/. %{buildroot}/

%files
%license %{_datadir}/licenses/%{name}
/opt/pdf-fire
%{_bindir}/pdf-fire
%{_datadir}/applications/pdf-fire.desktop
%{_datadir}/icons/hicolor/*/apps/pdf-fire.*

%changelog
* Thu Oct 01 2026 PDF Fire <pdffire.constant740@passmail.net> - 0.1.0-1
- Local build. See pdf-fire-PLAN.md for the change list.
