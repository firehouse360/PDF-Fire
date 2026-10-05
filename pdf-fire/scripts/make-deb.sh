#!/usr/bin/env bash
# PDF Fire — build the .deb from the release build. No sudo.
# Usage: scripts/make-deb.sh            → dist/pdf-fire_<version>_amd64.deb
set -euo pipefail

HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
SRC="$(cd "$HERE/../.." && pwd)"   # the source tree (this repository)
ROOT="${PDFFIRE_WORK_DIR:-$(dirname "$SRC")}"   # build/, deps/, dist/, repo/ (outside the repository)
BUILD="$ROOT/build/release"
PKGSRC="$SRC/packaging/pdf-fire"
VERSION="${PDFFIRE_VERSION:-0.1.5}"
ENGINE="$(sed -n 's/^set(PDF4QT_VERSION \(.*\))/\1/p' "$SRC/CMakeLists.txt" | tr -d '\r ')"
DEBVERSION="${VERSION}+pdf4qt${ENGINE}"
ARCH="$(dpkg --print-architecture)"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/pdffire-deb.XXXXXX")"
STAGE="$WORK/pdf-fire"
trap 'rm -rf "$WORK"' EXIT

"$SRC/pdf-fire/scripts/build.sh" release all release_translations
APP="$STAGE/opt/pdf-fire"
cmake --install "$BUILD" --prefix "$APP" > "$WORK/install.log"

# One launcher, one identity: drop what upstream installs for its five separate apps,
# the development headers, and the two apps PDF Fire does not ship.
rm -rf "$APP/include" "$APP/share/applications" "$APP/share/icons" "$APP/share/metainfo"
rm -f  "$APP/bin/Pdf4QtLaunchPad" "$APP/bin/Pdf4QtViewer"
# Unversioned .so links are only needed for linking against the libraries
find "$APP/lib" -maxdepth 1 -name 'libPdf4Qt*.so' -type l -delete

# Everything private lives in /opt/pdf-fire (the programs carry RPATH $ORIGIN/../lib, set in
# scripts/build.sh), so nothing can collide with a distribution package of PDF4QT.

install -Dm644 "$PKGSRC/pdf-fire.desktop" "$STAGE/usr/share/applications/pdf-fire.desktop"
cp -r "$PKGSRC/icons" "$STAGE/usr/share/"
mkdir -p "$STAGE/usr/bin"
ln -s /opt/pdf-fire/bin/Pdf4QtEditor "$STAGE/usr/bin/pdf-fire"
# The public key of the update repository: the package adds the repository itself (postinst),
# so an install from the downloaded file gets the updates too - as Chrome and VS Code do
install -Dm644 "$PKGSRC/pdf-fire-signing-key.asc" "$STAGE/usr/share/keyrings/pdf-fire-archive-keyring.asc"
install -Dm644 "$SRC/LICENSE" "$STAGE/usr/share/doc/pdf-fire/copyright"
cp -r "$SRC/3rdparty_licenses" "$STAGE/usr/share/doc/pdf-fire/"
printf 'pdf-fire (%s) unstable; urgency=medium\n\n  * Local build. See pdf-fire-PLAN.md for the change list.\n\n -- PDF Fire <pdffire.constant740@passmail.net>  %s\n' \
       "$DEBVERSION" "$(date -R)" | gzip -9n > "$STAGE/usr/share/doc/pdf-fire/changelog.gz"

find "$STAGE" -type f \( -name '*.so*' -o -path '*/bin/*' \) -exec strip --strip-unneeded {} + 2>/dev/null || true
find "$STAGE" -type d -exec chmod 755 {} +
find "$STAGE" -type f -perm /111 -exec chmod 755 {} +
find "$STAGE" -type f ! -perm /111 -exec chmod 644 {} +

# Library dependencies are computed from the binaries themselves
mkdir -p "$WORK/debian" && printf 'Source: pdf-fire\n\nPackage: pdf-fire\nArchitecture: any\n' > "$WORK/debian/control"
DEPENDS="$(cd "$WORK" && dpkg-shlibdeps -O --ignore-missing-info \
             -l"$APP/lib" $(find "$APP/bin" "$APP/lib" -type f) 2>/dev/null | sed 's/^shlibs:Depends=//')"

mkdir -p "$STAGE/DEBIAN"
cat > "$STAGE/DEBIAN/control" <<CONTROL
Package: pdf-fire
Version: $DEBVERSION
Section: text
Priority: optional
Architecture: $ARCH
Maintainer: PDF Fire <pdffire.constant740@passmail.net>
Installed-Size: $(du -sk "$STAGE/usr" "$STAGE/opt" | awk '{s+=$1} END {print s}')
Depends: $DEPENDS, fonts-urw-base35
Recommends: tesseract-ocr-eng, sane-airscan
Description: PDF editor - view, annotate, edit, sign, redact and protect PDF files
 PDF Fire is a native PDF editor for Linux, built on the PDF4QT engine.
 Annotations, form filling, page content editing, encryption and permissions,
 digital signatures, redaction, document compare and page organising.
CONTROL
cat > "$STAGE/DEBIAN/postinst" <<'POSTINST'
#!/bin/sh
set -e
SOURCES=/etc/apt/sources.list.d/pdf-fire.sources
if [ "$1" = configure ]; then
    command -v update-desktop-database >/dev/null && update-desktop-database -q /usr/share/applications || true
    command -v gtk-update-icon-cache >/dev/null && gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor || true

    # The update repository: added once, unless it is set up already (the commands of the
    # download page) or the administrator said no (REPO_ADD=false in /etc/default/pdf-fire)
    if [ -f /etc/default/pdf-fire ] && grep -qs '^REPO_ADD=false' /etc/default/pdf-fire; then
        :
    elif grep -rqs 'firehouse360.com/pdf-fire/apt' /etc/apt/sources.list /etc/apt/sources.list.d/; then
        :
    else
        cat > "$SOURCES" <<'SOURCES'
# Added by the pdf-fire package: PDF Fire updates arrive with the other updates.
# Removed again when pdf-fire is removed. To stop the package from adding it, put
# REPO_ADD=false into /etc/default/pdf-fire and delete this file.
Types: deb
URIs: https://firehouse360.com/pdf-fire/apt
Suites: stable
Components: main
Architectures: amd64
Signed-By: /usr/share/keyrings/pdf-fire-archive-keyring.asc
SOURCES
        chmod 644 "$SOURCES"
    fi
fi
POSTINST
cat > "$STAGE/DEBIAN/postrm" <<'POSTRM'
#!/bin/sh
set -e
command -v update-desktop-database >/dev/null && update-desktop-database -q /usr/share/applications || true
# The repository, which the package added (its key goes away with the package)
if [ "$1" = remove ] || [ "$1" = purge ]; then
    if grep -qs '^# Added by the pdf-fire package' /etc/apt/sources.list.d/pdf-fire.sources; then
        rm -f /etc/apt/sources.list.d/pdf-fire.sources
    fi
fi
POSTRM
chmod 755 "$STAGE/DEBIAN/postinst" "$STAGE/DEBIAN/postrm"

mkdir -p "$ROOT/dist"
OUT="$ROOT/dist/pdf-fire_${DEBVERSION}_${ARCH}.deb"
dpkg-deb --build --root-owner-group "$STAGE" "$OUT" > /dev/null
echo "built $OUT ($(du -h "$OUT" | cut -f1))"
