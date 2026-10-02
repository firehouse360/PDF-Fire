#!/usr/bin/env bash
# PDF Fire — build the three libraries Ubuntu does not package (Clipper2, asmjit, blend2d)
# into a project-local prefix. No sudo. Versions are pinned to the commits PDF4QT's own
# Flatpak manifest uses (Flatpak/io.github.JakubMelka.Pdf4qt.json).
set -euo pipefail

HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
SRC="$(cd "$HERE/../.." && pwd)"   # the source tree (this repository)
ROOT="${PDFFIRE_WORK_DIR:-$(dirname "$SRC")}"   # build/, deps/, dist/, repo/ (outside the repository)
SRC="$ROOT/deps/src"
PREFIX="$ROOT/deps/prefix"
BUILD="$ROOT/deps/build"
JOBS="${JOBS:-$(nproc)}"

common=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX"
        -DCMAKE_PREFIX_PATH="$PREFIX" -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DCMAKE_INSTALL_LIBDIR=lib)

# Same one-line fix the Flatpak manifest applies (uninitialised variable).
sed -i 's/int ec;/int ec = 0;/' "$SRC/Clipper2/CPP/Clipper2Lib/src/clipper.triangulation.cpp"

cmake -S "$SRC/Clipper2/CPP" -B "$BUILD/clipper2" "${common[@]}" \
      -DCLIPPER2_EXAMPLES=OFF -DCLIPPER2_TESTS=OFF -DCLIPPER2_UTILS=OFF
cmake --build "$BUILD/clipper2" -j "$JOBS"
cmake --install "$BUILD/clipper2"

cmake -S "$SRC/asmjit" -B "$BUILD/asmjit" "${common[@]}" -DASMJIT_STATIC=ON
cmake --build "$BUILD/asmjit" -j "$JOBS"
cmake --install "$BUILD/asmjit"

cmake -S "$SRC/blend2d" -B "$BUILD/blend2d" "${common[@]}" -DBLEND2D_EXTERNAL_ASMJIT=ON -DBLEND2D_STATIC=ON
cmake --build "$BUILD/blend2d" -j "$JOBS"
cmake --install "$BUILD/blend2d"

echo "deps installed to $PREFIX"
