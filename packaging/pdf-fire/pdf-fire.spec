# PDF Fire — Fedora package (binary repackaging).
#
# Not built from a tarball: scripts/make-rpm.sh compiles PDF Fire inside a Fedora container,
# against Fedora's own Qt and libraries, stages `cmake --install` under /opt/pdf-fire, and then
# runs rpmbuild on this spec in the same container with the stage passed in:
#
#   rpmbuild -bb pdf-fire.spec --define "pdffire_stage <dir>" --define "pdffire_version 0.1.3" \
#            --define "pdffire_engine 1.6.0.0" --define "pdffire_private_libs <regex>"
#
# Everything private lives in /opt/pdf-fire (the programs carry RPATH $ORIGIN/../lib), the same
# layout as the .deb from scripts/make-deb.sh, so nothing collides with a Fedora pdf4qt package.
# The package is left UNSIGNED on purpose — signing is a separate step.

%{!?pdffire_version: %global pdffire_version 0.1.3}
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

%post
# The update repository: added once, unless it is set up already (the commands of the
# download page) or the administrator said no (REPO_ADD=false in /etc/default/pdf-fire)
if [ -f /etc/default/pdf-fire ] && grep -qs '^REPO_ADD=false' /etc/default/pdf-fire; then
    :
elif grep -qs 'firehouse360.com/pdf-fire/rpm' /etc/yum.repos.d/*.repo; then
    :
else
    cat > /etc/yum.repos.d/pdf-fire.repo <<'REPO'
# Added by the pdf-fire package: PDF Fire updates arrive with the other updates.
# Removed again when pdf-fire is removed. To stop the package from adding it, put
# REPO_ADD=false into /etc/default/pdf-fire and delete this file.
[pdf-fire]
name=PDF Fire
baseurl=https://firehouse360.com/pdf-fire/rpm
enabled=1
gpgcheck=1
repo_gpgcheck=1
gpgkey=file:///etc/pki/rpm-gpg/RPM-GPG-KEY-pdf-fire
REPO
    chmod 644 /etc/yum.repos.d/pdf-fire.repo
fi
exit 0

%postun
# Removed (not upgraded): the repository, which the package added, goes too
if [ "$1" -eq 0 ] && grep -qs '^# Added by the pdf-fire package' /etc/yum.repos.d/pdf-fire.repo; then
    rm -f /etc/yum.repos.d/pdf-fire.repo
fi
exit 0

%files
%license %{_datadir}/licenses/%{name}
%{_sysconfdir}/pki/rpm-gpg/RPM-GPG-KEY-pdf-fire
/opt/pdf-fire
%{_bindir}/pdf-fire
%{_datadir}/applications/pdf-fire.desktop
%{_datadir}/icons/hicolor/*/apps/pdf-fire.*

%changelog
* Sat Oct 03 2026 PDF Fire <pdffire.constant740@passmail.net> - 0.1.3-1
- A start, which never finished, is noticed at the next start: the saved settings are set
  aside (kept as a backup) and PDF Fire starts with its defaults.
- Closing a document or PDF Fire while reading aloud stops the reading first; the sidebar
  is not restored without a document (a Windows PC froze at every start after that).

* Fri Oct 02 2026 PDF Fire <pdffire.constant740@passmail.net> - 0.1.2-1
- Read Aloud: the voice plays on the speaker the system uses (Qt 6.10's PipeWire backend
  did not see all outputs, e.g. Bluetooth speakers, and played into the wrong one).
- An install from the downloaded file adds the update repository, so updates arrive with
  the other updates (removed again with the package; REPO_ADD=false in /etc/default/pdf-fire).

* Fri Oct 02 2026 PDF Fire <pdffire.constant740@passmail.net> - 0.1.1-1
- Add Text: click anywhere on a page and type (typewriter text, editable later).
- Document Info with the protection status in plain words; a bar above protected documents;
  Protect > Enter Password (the permissions password unlocks the restricted tools).
- Form fields: no outline by default; fonts, text colour, dashed / underline borders,
  multi-line, scroll, comb, password and spelling options; quick options in the right-click menu.

* Thu Oct 01 2026 PDF Fire <pdffire.constant740@passmail.net> - 0.1.0-1
- Local build. See pdf-fire-PLAN.md for the change list.
