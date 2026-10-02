#!/usr/bin/env bash
# PDF Fire — configure + build. Usage: scripts/build.sh [release|debug|asan] [ninja targets...]
#   release  optimised build (default)
#   debug    -O0 -g, assertions on
#   asan     debug + AddressSanitizer + UndefinedBehaviorSanitizer (for bug hunting)
#   <kind>-<name>  the same kind in its own directory build/<kind>-<name> (parallel work, e.g. release-ocr)
set -euo pipefail

HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
SRC="$(cd "$HERE/../.." && pwd)"   # the source tree (this repository)
ROOT="${PDFFIRE_WORK_DIR:-$(dirname "$SRC")}"   # build/, deps/, dist/, repo/ (outside the repository)
KIND="${1:-release}"; shift || true
BUILD="$ROOT/build/$KIND"
JOBS="${JOBS:-14}"

args=(-S "$SRC" -B "$BUILD" -G Ninja
      -DCMAKE_PREFIX_PATH="$ROOT/deps/prefix"
      -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
      # installed under /opt/pdf-fire: the programs find their private libraries next to them
      "-DCMAKE_INSTALL_RPATH=\$ORIGIN/../lib"
      -DPDF4QT_INSTALL_INCLUDE=OFF)

case "${KIND%%-*}" in
  release) args+=(-DCMAKE_BUILD_TYPE=Release) ;;
  debug)   args+=(-DCMAKE_BUILD_TYPE=Debug) ;;
  asan)    args+=(-DCMAKE_BUILD_TYPE=Debug
                  "-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer"
                  "-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer"
                  "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined"
                  "-DCMAKE_SHARED_LINKER_FLAGS=-fsanitize=address,undefined"
                  "-DCMAKE_MODULE_LINKER_FLAGS=-fsanitize=address,undefined") ;;
  *) echo "unknown build kind: $KIND" >&2; exit 2 ;;
esac

[ -f "$BUILD/build.ninja" ] || cmake "${args[@]}"
cmake --build "$BUILD" -j "$JOBS" ${@:+--target "$@"}
