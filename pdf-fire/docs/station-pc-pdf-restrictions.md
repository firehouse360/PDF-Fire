# Making other apps honor PDF restrictions (station computers)

*Research 2026-10-01. NOTHING here has been applied to any computer. Each step needs `sudo` on the
computer it is for.*

## The short version

- PDF permissions ("no printing", "no copying", "no changes") are **requests to the reader**. The PDF
  standard says so (ISO 32000-1 §7.6.3.1: readers "shall respect the intent", nothing in the file enforces it).
- On **computers the department controls**, Firefox and the GNOME document viewer can be set to obey
  them, and the setting can be locked. Chrome/Chromium and Okular already obey them.
- It does **not** bind a file once it leaves the department. A person can always use another app, or
  `qpdf --decrypt`. Real protection is the **open password**. To make changes visible, **sign or certify**.

## Which apps honor them

| App | Honors by default? | Can it be forced? |
|---|---|---|
| **PDF Fire** | Yes (all tools, 2026-10-01) | — |
| **Adobe Acrobat / Reader** | Yes | — |
| **Chrome / Chromium** (and probably Edge) | Yes: print and copy ([source](https://chromium.googlesource.com/chromium/src/+/main/pdf/pdfium/pdfium_permissions.cc)) | Already on |
| **Okular** (KDE) | Yes (`ObeyDRM`, default true) | KDE Kiosk lock |
| **Firefox** (pdf.js) | **No** (`pdfjs.enablePermissions` = false, [source](https://github.com/mozilla/pdf.js/blob/master/web/app_options.js)) | **Yes**, with a locked enterprise policy (below) |
| **GNOME Papers** (Ubuntu 26.04 default viewer; replaced Evince) | **No** (`override-restrictions` = true) | **Yes**, with a locked dconf setting (below) |
| macOS Preview, iOS, Android viewers | Not verified | Only with device management (MDM) |

With the Firefox setting on, it blocks printing, copying text, its annotation editor (when changes are
denied) and form filling (when forms are denied).

## Station Linux PC — Firefox (works for the Ubuntu snap too)

Create `/etc/firefox/policies/policies.json` (with sudo):

```json
{ "policies": { "Preferences": {
  "pdfjs.enablePermissions": { "Value": true, "Status": "locked", "Type": "boolean" } } } }
```

Then restart Firefox and check `about:policies`. Windows: the same setting through Group Policy, or
`distribution\policies.json` in the Firefox folder. Mozilla's documentation is
[here](https://github.com/mozilla/policy-templates/blob/master/docs/index.md#preferences).

## Station Linux PC — GNOME Papers (the default PDF viewer)

Three files (with sudo):

```
# /etc/dconf/profile/user
user-db:user
system-db:local

# /etc/dconf/db/local.d/00-papers
[org/gnome/papers]
override-restrictions=false

# /etc/dconf/db/local.d/locks/papers
/org/gnome/papers/override-restrictions
```

Then run `sudo dconf update` and log out and back in. If Evince is also installed, add the same lines for
`org/gnome/evince`.

## What still gets around it

- Saving the file and opening it in another app or on another computer.
- Free tools that strip the restrictions (`qpdf --decrypt`).

## For files that leave the department

| Need | Use |
|---|---|
| Strangers must not read it | **Open password** (AES-256). Send the password by a separate channel. |
| Any change must be visible | **Sign** it, or **Certify** it (Sign ▸ Certify, "no changes allowed"). |
| Real print/copy control everywhere | Only server-based rights management, such as Microsoft Purview (needs Microsoft 365 business licences, about $22/user/month, not verified) or Adobe's enterprise service. Not realistic for a small VFD. |
