#!/usr/bin/env bash
# PDF Fire — run a UI script against the editor, headless, SAFELY.
#
# The document is COPIED into a fresh sandbox directory and the editor works on the copy, with
# its own settings directory. The editor refuses to run a script on a file outside the sandbox.
# (2026-09-30: a script run directly on a user's document answered its "save changes?" question
# and overwrote the document. Never again.)
#
# Usage: scripts/uitest.sh <out-dir> "<script>" [document.pdf] [release|asan] [--theme-dark|--theme-light] [--user-settings]
#   In the script, "{OUT}" is replaced by the output directory (shot:{OUT}/name.png).
#   --user-settings starts from a COPY of the user's settings file.
set -euo pipefail
HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
SRC="$(cd "$HERE/../.." && pwd)"   # the source tree (this repository)
ROOT="${PDFFIRE_WORK_DIR:-$(dirname "$SRC")}"   # build/, deps/, dist/, repo/ (outside the repository)
OUT="$1"; SCRIPT="$2"; DOC="${3:-}"; KIND="${4:-release}"; THEME="${5:---theme-dark}"; USERCFG="${6:-}"
mkdir -p "$OUT"
SANDBOX="$(mktemp -d "$OUT/sandbox.XXXXXX")"
mkdir -p "$SANDBOX/cfg/PDF Fire" "$SANDBOX/data" "$SANDBOX/doc"
if [ "$USERCFG" = "--user-settings" ] && [ -f "$HOME/.config/PDF Fire/PDF Fire.ini" ]; then
  cp "$HOME/.config/PDF Fire/PDF Fire.ini" "$SANDBOX/cfg/PDF Fire/"
fi
# UITEST_CERT_DIR: test certificates (*.pfx) copied into the sandbox - never the user's own
if [ -n "${UITEST_CERT_DIR:-}" ]; then
  mkdir -p "$SANDBOX/data/PDF Fire/PDF Fire/certificates"
  cp "$UITEST_CERT_DIR"/*.pfx "$SANDBOX/data/PDF Fire/PDF Fire/certificates/"
fi
ARGS=("$THEME")
if [ -n "$DOC" ]; then
  cp "$DOC" "$SANDBOX/doc/"
  ARGS+=("$SANDBOX/doc/$(basename "$DOC")")
fi
XDG_CONFIG_HOME="$SANDBOX/cfg" XDG_DATA_HOME="$SANDBOX/data" QT_QPA_PLATFORM=offscreen \
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" PDFFIRE_SCRIPT_SANDBOX="$SANDBOX" PDFFIRE_SCRIPT="${SCRIPT//\{OUT\}/$OUT}" \
  timeout "${UITEST_TIMEOUT:-120}" "$ROOT/build/$KIND/bin/Pdf4QtEditor" "${ARGS[@]}" > "$SANDBOX/app.log" 2>&1 \
  && STATUS=0 || STATUS=$?
grep -vE 'propagateSizeHints|does not support raise|grabbing the keyboard' "$SANDBOX/app.log" || true
# A crash must never pass unnoticed (2026-10-01: a crash was hidden by the filter above)
if [ "$STATUS" -ne 0 ]; then echo "UITEST: the editor exited with status $STATUS (139 = crash, 124 = timeout)"; fi
echo "sandbox: $SANDBOX"
