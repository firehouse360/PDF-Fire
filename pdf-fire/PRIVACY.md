# PDF Fire — Privacy

PDF Fire collects nothing about you and sends nothing anywhere on its own. Your documents stay on your computer.
There are no accounts, no analytics, no crash reports and no automatic update checks inside the program
(updates on Linux come through your system's package manager).

**This program will not transfer any information to other networked systems unless specifically requested by
the user.** These features use the network, and only when you use them:

| Feature | What is sent, and where |
|---|---|
| **Signing with a timestamp** (Sign dialog, "Signature with timestamp") | A one-way fingerprint (hash) of the signature — not the document — to the timestamp authority you choose (by default DigiCert, Sectigo or FreeTSA), which returns a signed timestamp. |
| **Signing record: "public address"** (off unless you tick it, or your department's certificate turns it on) | A request to `api.ipify.org`, which answers with your computer's public internet address; the address is then written into the signature. |
| **Send by e-mail** | The document and the message you write, to the mail (SMTP) server you configure. |
| **Read aloud** | Nothing — the natural voices run on your computer, offline. |
| **Text recognition (OCR), scanning, redaction** | Nothing — everything runs on your computer. |

Checking signatures uses only the information in the document and the certificates on your computer;
PDF Fire does not download revocation lists or certificates when you open a file.

Questions: pdffire.constant740@passmail.net · Supporting PDF Fire on our web site (optional, separate from the
program) is covered by the [Firehouse 360 privacy policy](https://firehouse360.com/privacy) and
[Square's privacy notice](https://squareup.com/us/en/legal/general/privacy).
