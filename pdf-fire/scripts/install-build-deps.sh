#!/usr/bin/env bash
# PDF Fire — one-time install of the build toolchain and libraries (Ubuntu 26.04).
# Needs sudo. Launched in a ptyxis window so the password can be typed.
set -u

PKGS=(
  # toolchain
  build-essential cmake ninja-build pkg-config ccache
  # Qt 6
  qt6-base-dev qt6-base-dev-tools qt6-svg-dev qt6-speech-dev
  qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools libgl1-mesa-dev
  # PDF4QT engine dependencies
  libssl-dev zlib1g-dev libfreetype-dev libopenjp2-7-dev libjpeg-dev
  libpng-dev liblcms2-dev libtbb-dev libsane-dev libfontconfig-dev libcups2-dev
  # PDF Fire additions: OCR + encryption/page tooling
  tesseract-ocr tesseract-ocr-eng libtesseract-dev libleptonica-dev
  qpdf libqpdf-dev
  # code checking and debugging
  clang clang-tidy cppcheck gdb valgrind
  # .deb packaging
  dpkg-dev fakeroot lintian
)

echo "PDF Fire: installing ${#PKGS[@]} packages with apt."
echo
sudo apt-get update
sudo apt-get install -y "${PKGS[@]}"
rc=$?

echo
if [ $rc -eq 0 ]; then
  echo "DONE — all packages installed."
  touch "${PDFFIRE_WORK_DIR:-$(dirname "$(cd "$(dirname "$(readlink -f "$0")")/../.." && pwd)")}/.build-deps-installed"
else
  echo "apt exited with code $rc — scroll up for the error."
fi
read -r -p "Press Enter to close this window. "
