#!/usr/bin/env bash
# PDF Fire — build the Fedora .rpm in a rootless podman container. No sudo, no signing.
# Usage: scripts/make-rpm.sh            build + package   → dist/pdf-fire-<version>-1.fc44.x86_64.rpm
#        scripts/make-rpm.sh verify     install the .rpm in a FRESH Fedora container and start it
#        scripts/make-rpm.sh all        both
#
# Environment:
#   PDFFIRE_VERSION  package version (default 0.1.9, same as make-deb.sh)
#   FEDORA           Fedora release (default 44)
#   FEDORA_IMAGE     base image (default registry.fedoraproject.org/fedora:$FEDORA, falls back to
#                    quay.io/fedora/fedora:$FEDORA when the Fedora registry is down)
#   JOBS             parallel compile jobs (default 14)
#   SKIP_TESTS=1     do not run ctest in the build container
#
# Everything this writes lives in its own directories, so the Ubuntu builds are never touched:
#   build/fedora<N>-deps     Clipper2, asmjit, blend2d built with Fedora's compiler (static)
#   build/fedora<N>-release  the PDF Fire build tree
#   build/fedora<N>-ccache   compiler cache, so a rebuild is quick
#   dist/                    the .rpm (unsigned — signing is a separate step)
#
# The container mounts the project at the same path it has here, runs as root inside a rootless
# user namespace (so every file it writes is owned by you on the host) and never needs sudo.
set -euo pipefail

HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
SRC="$(cd "$HERE/../.." && pwd)"   # the source tree (this repository)
ROOT="${PDFFIRE_WORK_DIR:-$(dirname "$SRC")}"   # build/, deps/, dist/, repo/ (outside the repository)
VERSION="${PDFFIRE_VERSION:-0.1.9}"
FEDORA="${FEDORA:-44}"
JOBS="${JOBS:-14}"
MODE="${1:-build}"

# ---------------------------------------------------------------------------------------------
# Inside the build container: deps → configure → build → test → stage → rpmbuild
# ---------------------------------------------------------------------------------------------
inside_build() {
  local DEPS="$ROOT/build/fedora$FEDORA-deps" BUILD="$ROOT/build/fedora$FEDORA-release"
  local PKGSRC="$SRC/packaging/pdf-fire"
  local ENGINE; ENGINE="$(sed -n 's/^set(PDF4QT_VERSION \(.*\))/\1/p' "$SRC/CMakeLists.txt" | tr -d '\r ')"
  export CCACHE_DIR="$ROOT/build/fedora$FEDORA-ccache"

  # 1. The three libraries Fedora does not package at the commits PDF4QT pins (Fedora has a
  #    blend2d/asmjit, but at other versions, and no Clipper2) — same recipe as build-deps.sh,
  #    from the same deps/src checkouts, into a Fedora-only prefix.
  if [ ! -f "$DEPS/prefix/lib/cmake/blend2d/blend2d-config.cmake" ] && \
     [ ! -f "$DEPS/prefix/lib/cmake/blend2d/blend2dConfig.cmake" ]; then
    local common=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$DEPS/prefix"
                  -DCMAKE_PREFIX_PATH="$DEPS/prefix" -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                  -DCMAKE_INSTALL_LIBDIR=lib)
    # build-deps.sh applies the Flatpak manifest's one-line fix; check it is there (read-only)
    grep -q 'int ec = 0;' "$ROOT/deps/src/Clipper2/CPP/Clipper2Lib/src/clipper.triangulation.cpp" || \
      { echo "deps/src/Clipper2 lacks the 'int ec = 0;' fix — run scripts/build-deps.sh once" >&2; exit 1; }
    cmake -S "$ROOT/deps/src/Clipper2/CPP" -B "$DEPS/build/clipper2" "${common[@]}" \
          -DCLIPPER2_EXAMPLES=OFF -DCLIPPER2_TESTS=OFF -DCLIPPER2_UTILS=OFF
    cmake --build "$DEPS/build/clipper2" -j "$JOBS" && cmake --install "$DEPS/build/clipper2"
    cmake -S "$ROOT/deps/src/asmjit" -B "$DEPS/build/asmjit" "${common[@]}" -DASMJIT_STATIC=ON
    cmake --build "$DEPS/build/asmjit" -j "$JOBS" && cmake --install "$DEPS/build/asmjit"
    cmake -S "$ROOT/deps/src/blend2d" -B "$DEPS/build/blend2d" "${common[@]}" \
          -DBLEND2D_EXTERNAL_ASMJIT=ON -DBLEND2D_STATIC=ON
    cmake --build "$DEPS/build/blend2d" -j "$JOBS" && cmake --install "$DEPS/build/blend2d"
  fi

  # 2. PDF Fire itself. Same options as scripts/build.sh release, plus CMAKE_INSTALL_LIBDIR=lib:
  #    GNUInstallDirs would pick lib64 on Fedora, and the programs look in $ORIGIN/../lib.
  if [ ! -f "$BUILD/build.ninja" ]; then
    cmake -S "$SRC" -B "$BUILD" -G Ninja \
          -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_PREFIX_PATH="$DEPS/prefix" \
          -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
          -DCMAKE_INSTALL_LIBDIR=lib \
          "-DCMAKE_INSTALL_RPATH=\$ORIGIN/../lib" \
          -DPDF4QT_INSTALL_INCLUDE=OFF
  fi
  cmake --build "$BUILD" -j "$JOBS" --target all release_translations

  # 3. Unit tests (headless). A failure is reported but does not stop the package.
  if [ "${SKIP_TESTS:-0}" != 1 ]; then
    (cd "$BUILD" && QT_QPA_PLATFORM=offscreen ctest --output-on-failure --timeout 600 -j 4) \
      2>&1 | tee "$BUILD/ctest.log" || echo "WARNING: some unit tests failed — see $BUILD/ctest.log"
  fi

  # 4. Stage — mirrors make-deb.sh: one launcher, one identity
  local WORK; WORK="$(mktemp -d /tmp/pdffire-rpm.XXXXXX)"
  local STAGE="$WORK/stage" APP="$WORK/stage/opt/pdf-fire"
  cmake --install "$BUILD" --prefix "$APP" > "$WORK/install.log"
  rm -rf "$APP/include" "$APP/share/applications" "$APP/share/icons" "$APP/share/metainfo"
  rm -f  "$APP/bin/Pdf4QtLaunchPad" "$APP/bin/Pdf4QtViewer"
  find "$APP/lib" -maxdepth 1 -name 'libPdf4Qt*.so' -type l -delete

  install -Dm644 "$PKGSRC/pdf-fire.desktop" "$STAGE/usr/share/applications/pdf-fire.desktop"
  # The public key of the update repository (%post adds the repository - see pdf-fire.spec)
  install -Dm644 "$PKGSRC/pdf-fire-signing-key.asc" "$STAGE/etc/pki/rpm-gpg/RPM-GPG-KEY-pdf-fire"
  mkdir -p "$STAGE/usr/share" && cp -r "$PKGSRC/icons" "$STAGE/usr/share/"
  mkdir -p "$STAGE/usr/bin" && ln -s ../../opt/pdf-fire/bin/Pdf4QtEditor "$STAGE/usr/bin/pdf-fire"  # relative, as rpmlint wants
  local LIC="$STAGE/usr/share/licenses/pdf-fire"
  install -Dm644 "$SRC/LICENSE" "$LIC/LICENSE"
  cp -r "$SRC/3rdparty_licenses" "$LIC/"

  find "$STAGE" -type f \( -name '*.so*' -o -path '*/bin/*' \) -exec strip --strip-unneeded {} + 2>/dev/null || true
  find "$STAGE" -type d -exec chmod 755 {} +
  find "$STAGE" -type f -perm /111 -exec chmod 755 {} +
  find "$STAGE" -type f ! -perm /111 -exec chmod 644 {} +

  # The sonames of the private libraries, so rpm does not turn them into Requires
  local sonames
  sonames="$(find "$APP" -type f -name '*.so*' -exec objdump -p {} + 2>/dev/null \
             | awk '/SONAME/ {print $2}' | sort -u | sed 's/[.]/[.]/g' | paste -sd'|')"

  # 5. rpmbuild — binary repackaging of the stage, built in this same Fedora
  local TOP="$WORK/rpmbuild" extra=()
  [ -n "$sonames" ] && extra=(--define "pdffire_private_libs ^(${sonames})")
  rpmbuild -bb "$PKGSRC/pdf-fire.spec" \
           --define "_topdir $TOP" \
           --define "pdffire_stage $STAGE" \
           --define "pdffire_version $VERSION" \
           --define "pdffire_engine $ENGINE" "${extra[@]}"
  mkdir -p "$ROOT/dist"
  local rpm; rpm="$(find "$TOP/RPMS" -name '*.rpm' | head -1)"
  cp -f "$rpm" "$ROOT/dist/"
  echo
  rpm -qp --requires "$rpm" | sed 's/^/  requires: /'
  echo "built $ROOT/dist/$(basename "$rpm") ($(du -h "$rpm" | cut -f1))"
  rm -rf "$WORK"
}

# ---------------------------------------------------------------------------------------------
# Inside a FRESH container (no build deps): install the .rpm and prove it starts
# ---------------------------------------------------------------------------------------------
inside_verify() {
  local rpm; rpm="$(ls -t "$ROOT"/dist/pdf-fire-"$VERSION"-*.fc"$FEDORA".x86_64.rpm | head -1)"
  echo "== dnf install $(basename "$rpm")"
  dnf -y install --nogpgcheck "$rpm" > /tmp/dnf.log 2>&1 || { tail -40 /tmp/dnf.log; exit 1; }
  grep -E '^ (pdf-fire|urw-base35-d050000l-fonts|tesseract|tesseract-langpack-eng|sane-airscan|qt6-qtbase|qt6-qtmultimedia) ' /tmp/dnf.log
  rpm -q pdf-fire
  echo "== missing libraries (ldd)"
  # The plugins (lib/pdf4qt) are dlopen()ed by the editor, which has already loaded the engine
  # libraries; checked standalone their $ORIGIN/../lib points one level too deep, so they are
  # checked with the engine directory on the path, as they are in the running program.
  local missing
  missing="$( { find /opt/pdf-fire/bin /opt/pdf-fire/lib -maxdepth 1 -type f -exec ldd {} + ;
                LD_LIBRARY_PATH=/opt/pdf-fire/lib find /opt/pdf-fire/lib/pdf4qt -type f -exec ldd {} + ; } \
              2>/dev/null | grep 'not found' || true)"
  [ -z "$missing" ] && echo "none" || { echo "$missing"; exit 1; }
  echo "== fc-match D050000L"
  fc-match D050000L
  fc-match ZapfDingbats
  echo "== Pdf4QtEditor --help"
  QT_QPA_PLATFORM=offscreen /opt/pdf-fire/bin/Pdf4QtEditor --help 2>&1 | head -20 || true
  echo "== start the editor with a document (offscreen, 8 s)"
  # A copy in /tmp — never a real document
  local pdf=/tmp/verify.pdf
  printf '%%PDF-1.4\n1 0 obj<</Type/Catalog/Pages 2 0 R>>endobj\n2 0 obj<</Type/Pages/Kids[3 0 R]/Count 1>>endobj\n3 0 obj<</Type/Page/Parent 2 0 R/MediaBox[0 0 612 792]>>endobj\ntrailer<</Root 1 0 R>>\n%%%%EOF\n' > "$pdf"
  export HOME=/tmp/verify-home; mkdir -p "$HOME"
  QT_QPA_PLATFORM=offscreen QT_DEBUG_PLUGINS=1 /usr/bin/pdf-fire "$pdf" > /tmp/run.log 2>&1 &
  local pid=$!
  sleep 8
  kill -0 "$pid" 2>/dev/null || { echo "FAIL: the editor exited"; tail -30 /tmp/run.log; exit 1; }
  echo "still running after 8 s (pid $pid). Mapped into the process:"
  # What the running editor actually loaded: its own plugins, the Qt platform plugin, ONNX Runtime
  grep -oE '/[^ ]*(/pdf4qt/lib[A-Za-z]+Plugin|libqoffscreen|libonnxruntime|libPdf4QtLib[A-Za-z]+|libtesseract|libsane)[^ ]*' \
       "/proc/$pid/maps" | sort -u | sed 's/^/  /'
  grep -iE 'cannot load|failed to|not found' /tmp/run.log | sort -u | head -20 || true
  kill "$pid"; wait "$pid" 2>/dev/null || true
  echo "== installed size"; du -sh /opt/pdf-fire
  echo "VERIFY OK"
}

case "$MODE" in
  --inside-build)  inside_build;  exit ;;
  --inside-verify) inside_verify; exit ;;
esac

# ---------------------------------------------------------------------------------------------
# On the host: rootless podman only
# ---------------------------------------------------------------------------------------------
command -v podman >/dev/null || { echo "podman is not installed" >&2; exit 1; }
[ "$(podman info --format '{{.Host.Security.Rootless}}')" = true ] || \
  { echo "podman is not running rootless — refusing" >&2; exit 1; }

base_image() {
  if [ -n "${FEDORA_IMAGE:-}" ]; then echo "$FEDORA_IMAGE"; return; fi
  local img
  for img in "registry.fedoraproject.org/fedora:$FEDORA" "quay.io/fedora/fedora:$FEDORA"; do
    if podman image exists "$img" || podman pull -q "$img" >/dev/null 2>&1; then echo "$img"; return; fi
  done
  echo "could not pull a fedora:$FEDORA image" >&2; exit 1
}
BASE="$(base_image)"
BUILDER="localhost/pdf-fire-build:fc$FEDORA"
# The source tree is mounted too, when it is not inside the work folder (CI)
srcmount=(); srcmount_ro=(); case "$SRC/" in "$ROOT"/*) ;; *) srcmount=(-v "$SRC:$SRC"); srcmount_ro=(-v "$SRC:$SRC:ro") ;; esac
run=(podman run --rm --network=host -v "$ROOT:$ROOT" "${srcmount[@]}" -w "$ROOT" -e PDFFIRE_WORK_DIR="$ROOT"
     -e PDFFIRE_VERSION="$VERSION" -e FEDORA="$FEDORA" -e JOBS="$JOBS" -e SKIP_TESTS="${SKIP_TESTS:-0}")

do_build() {
  # The toolchain image is cached as a local image; it is rebuilt only when this list changes
  podman build -t "$BUILDER" --build-arg BASE="$BASE" -f - "$HERE" <<'CONTAINERFILE'
ARG BASE
FROM ${BASE}
RUN dnf -y install --setopt=install_weak_deps=False \
      gcc-c++ cmake ninja-build pkgconf-pkg-config ccache rpm-build binutils findutils file \
      qt6-qtbase-devel qt6-qtbase-private-devel qt6-qtsvg-devel qt6-qttools-devel qt6-linguist \
      qt6-qtmultimedia-devel qt6-qtspeech-devel mesa-libGL-devel \
      openssl-devel zlib-ng-compat-devel freetype-devel openjpeg-devel libjpeg-turbo-devel \
      libpng-devel lcms2-devel tbb-devel fontconfig-devel cups-devel \
      tesseract-devel leptonica-devel sane-backends-devel \
 && dnf clean all
CONTAINERFILE
  "${run[@]}" "$BUILDER" bash "$SRC/pdf-fire/scripts/make-rpm.sh" --inside-build
}

do_verify() {
  # Fresh base image, project mounted read-only: nothing but the .rpm and what dnf pulls in for it
  podman run --rm --network=host -v "$ROOT:$ROOT:ro" "${srcmount_ro[@]}" -w / -e PDFFIRE_WORK_DIR="$ROOT" -e PDFFIRE_VERSION="$VERSION" -e FEDORA="$FEDORA" "$BASE" bash "$SRC/pdf-fire/scripts/make-rpm.sh" --inside-verify
}

case "$MODE" in
  build)  do_build ;;
  verify) do_verify ;;
  all)    do_build; do_verify ;;
  *) echo "usage: $0 [build|verify|all]" >&2; exit 2 ;;
esac
