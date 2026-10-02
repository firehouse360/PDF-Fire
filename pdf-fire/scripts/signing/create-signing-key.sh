#!/usr/bin/env bash
# PDF Fire — create the release signing key (run ONCE, in a terminal, by the person who
# will keep the passphrase). The key signs the .deb repository and the .rpm packages;
# Fedora 45 refuses unsigned packages, and apt refuses an unsigned repository.
#
# The key lives in its own GnuPG folder (not your personal keyring):
#     ~/.local/share/pdf-fire-signing/gnupg
# Afterwards, put the BACKUP file it writes into Proton Drive (online only) and on the
# encrypted USB stick in the station safe. The passphrase goes on paper in the safe.
set -euo pipefail

HERE="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
SRC="$(cd "$HERE/../../.." && pwd)"   # the source tree (this repository)
ROOT="${PDFFIRE_WORK_DIR:-$(dirname "$SRC")}"   # build/, deps/, dist/, repo/ (outside the repository)
export GNUPGHOME="${PDFFIRE_SIGNING_HOME:-$HOME/.local/share/pdf-fire-signing/gnupg}"
IDENTITY="${PDFFIRE_SIGNING_IDENTITY:-PDF Fire Release Signing (Prospect VFD) <station@prospectfd.com>}"
PUBLIC_DIR="${PDFFIRE_SIGNING_DIR:-$ROOT/signing}"
BACKUP="$HOME/Documents/PDF-Fire-signing-key-BACKUP-SECRET.asc"

mkdir -p "$GNUPGHOME" "$PUBLIC_DIR"
chmod 700 "$GNUPGHOME"

if gpg --list-secret-keys "$IDENTITY" >/dev/null 2>&1; then
    echo "A signing key for '$IDENTITY' already exists in $GNUPGHOME - nothing to do."
else
    echo "Creating the PDF Fire signing key for: $IDENTITY"
    echo "You will be asked for a PASSPHRASE (twice). Use a few unrelated words, at least 12 characters."
    echo "Write it down for the station safe - without it, no new release can be signed."
    echo
    # RSA 4096: verified by every apt and rpm version; valid for 5 years (renewable)
    gpg --pinentry-mode loopback --quick-generate-key "$IDENTITY" rsa4096 sign 5y
fi

FINGERPRINT="$(gpg --with-colons --list-secret-keys "$IDENTITY" | awk -F: '/^fpr:/ {print $10; exit}')"
gpg --armor --export "$FINGERPRINT" > "$PUBLIC_DIR/pdf-fire-signing-key.asc"
echo "$FINGERPRINT" > "$PUBLIC_DIR/pdf-fire-signing-key.fingerprint"

echo
echo "Exporting the BACKUP of the secret key (still protected by the passphrase)..."
gpg --pinentry-mode loopback --armor --export-secret-keys "$FINGERPRINT" > "$BACKUP"
chmod 600 "$BACKUP"

echo
echo "Done."
echo "  Public key (share freely):  $PUBLIC_DIR/pdf-fire-signing-key.asc"
echo "  Fingerprint:                $(echo "$FINGERPRINT" | sed 's/.\{4\}/& /g')"
echo "  SECRET backup:              $BACKUP"
echo "    -> copy it to Proton Drive (online only) and the USB stick in the safe,"
echo "       then delete it from Documents."
read -r -p "Press Enter to close this window. " _ || true
