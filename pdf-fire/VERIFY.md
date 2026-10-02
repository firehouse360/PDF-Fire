# PDF Fire — Checking your download

## Linux (.deb and .rpm)

The packages and the update repository are signed with the PDF Fire release key:

    1AD3 AB56 3A5F C1D4 F403 632A C011 9E5C 2237 8B81

Your package manager shows this fingerprint when you add the key ([public key](https://firehouse360.com/pdf-fire/pdf-fire-signing-key.asc)) —
it must match. After that, `apt` and `dnf` check every package and update automatically.

## Windows (installer)

The Windows installer is **not code-signed** yet, so Windows shows *"Windows protected your PC"* when it starts.
Click **More info → Run anyway**. The installer is built from this repository by its automated build on GitHub
Actions ([`.github/workflows/pdf-fire-windows.yml`](../.github/workflows/pdf-fire-windows.yml)), and its SHA-256
checksum is published with each release. To check it in PowerShell:

    Get-FileHash .\PDF-Fire-Setup-<version>-x64.exe -Algorithm SHA256

The result must match the checksum on the release page.

## Privacy

PDF Fire sends nothing over the network unless you ask it to — see [PRIVACY.md](PRIVACY.md).
