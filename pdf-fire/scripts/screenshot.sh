#!/usr/bin/env bash
# PDF Fire — headless screenshots of the editor window, for checking the UI.
# Usage: scripts/screenshot.sh <out-dir> <file.pdf> [dark|light] [tab indexes...]
set -euo pipefail
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
SRC="$(cd "$HERE/../.." && pwd)"   # the source tree (this repository)
ROOT="${PDFFIRE_WORK_DIR:-$(dirname "$SRC")}"   # build/, deps/, dist/, repo/ (outside the repository)
OUT="$1"; DOC="$2"; THEME="${3:-dark}"; shift 3 || shift $#
TABS=("${@:-0}")
mkdir -p "$OUT/.cfg" "$OUT/.data" "$OUT/.doc"
# The editor works on a COPY of the document, never on the document itself
cp "$DOC" "$OUT/.doc/"
COPY="$OUT/.doc/$(basename "$DOC")"
for tab in "${TABS[@]}"; do
  XDG_CONFIG_HOME="$OUT/.cfg" XDG_DATA_HOME="$OUT/.data" QT_QPA_PLATFORM=offscreen PDFFIRE_SCRIPT_SANDBOX="$OUT/.doc" \
  PDFFIRE_SCREENSHOT="$OUT/$THEME-tab$tab.png" PDFFIRE_SCREENSHOT_TAB="$tab" \
    timeout 40 "$ROOT/build/release/bin/Pdf4QtEditor" "--theme-$THEME" "$COPY" 2>&1 | grep -v propagateSizeHints || true
done
