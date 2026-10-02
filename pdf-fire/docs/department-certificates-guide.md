# Department signing certificates — how it works (PDF Fire)

*Written 2026-10-01. Applies to any department: nothing in PDF Fire is tied to one department.*

## The idea in one paragraph

The department creates one **master certificate** (the "authority"). The master issues each member a
**signing certificate**. When a member signs a PDF, the signature carries their certificate *and* the
department's master certificate. Any computer that has **trusted the master once** shows every member's
signature as valid and trusted. A fake "Prospect VFD" master made by somebody else is NOT trusted:
trust is tied to the master's fingerprint, not its name.

## Who does what

| Who | Does what | How often |
|---|---|---|
| **Certificate officer** (Chief / Secretary) | Creates the authority; issues certificates; revokes them; renews the revocation list | Create once; issue per member; renew yearly |
| **Member** | Imports their certificate; signs | Once, then every signing |
| **Anyone who reads the department's PDFs** (township, insurer, other members) | Trusts the master certificate once | Once per computer |

## 1. Create the authority (certificate officer, once)

1. PDF Fire ▸ **Sign** ▸ **Department** ▸ **Create Authority…**
2. Name, e.g. *Prospect VFD Signing Authority*; organization, e.g. *Prospect VFD*.
3. **Revocation list address**: a web address where you will upload the revocation list, e.g.
   `https://prospectfd.com/pki/authority.crl`. It is written into every certificate and **cannot be changed
   later** (only by issuing new certificates). Leave it empty if you will not publish one.
4. Folder and a **passphrase** of at least 12 characters (a few unrelated words).
5. Answer **Yes** to "Trust it on this computer now?".

**Store it safely.** The folder holds `authority-private.p12` (the secret key, encrypted with the passphrase),
`authority.cer` (public; share freely), `authority.crl` (the revocation list), and `register.json` (the list
of issued certificates).

- Keep the whole folder in **Proton Drive, online-only** (not synced to computers), plus a copy on an
  **encrypted USB stick in the station safe**.
- Keep the **passphrase on paper in the safe**, never in the same place as the folder.
- One or two officers know the passphrase. Turn on two-factor login for the Proton account.

## 2. Issue a member certificate (certificate officer)

1. **Identify the member in person.** That is what makes their signatures hold up later.
2. Sign ▸ Department ▸ **Open Authority…** (choose the folder, type the passphrase) ▸ **Issue Certificate…**
3. Name, rank/title, e-mail (optional), and **valid for** (default 2 years).
4. **Recorded when this member signs:** operating system, computer name/user, IP addresses, public IP.
   They are all on by default. Tick **"The member can't turn these off"** to make them required.
5. PDF Fire shows a **password once**. Give the member the `.pfx` file and the password, separately if
   possible. The password is not stored anywhere.

## 3. Member setup (each member, once)

1. Sign ▸ **My Certificates** ▸ **Import** ▸ choose the `.pfx` file.
2. PDF Fire asks **"Trust Prospect VFD Signing Authority on this computer?"**. Answer **Yes**.
3. To sign: **Sign with Certificate**, type the password, and tick the consent box.
   - "Signature with timestamp" is the default. It needs internet; if there is none, PDF Fire offers to
     sign without the timestamp.

## 4. Trust the department on other computers (once per computer)

Give them `authority.cer` (Department ▸ **Share Authority Certificate…**). Read the **fingerprint** to them
or print it, so they can check they got the real one.

- **PDF Fire:** Sign ▸ Department ▸ **Trust an Authority…** ▸ choose `authority.cer`, and compare the fingerprint.
- **Adobe Acrobat / Reader:** Preferences ▸ Signatures ▸ Identities & Trusted Certificates ▸ More ▸
  Trusted Certificates ▸ Import `authority.cer` ▸ Edit Trust ▸ tick **"Use this certificate as a trusted
  root"**.
- **A station computer for every user (administrator):** copy `authority.cer` (and `authority.crl`) into
  `/etc/pdf-fire/trusted-authorities/`.

## 5. A member leaves, or a laptop is stolen

1. Department ▸ **Open Authority…** ▸ select the member ▸ **Revoke…** ▸ choose the reason.
2. Upload the new `authority.crl` to the revocation list address.

What readers then see:
- **Signatures made before the revocation with a timestamp stay valid**, with a note "revoked on …
  after this signature was made". That is why the timestamp is the default.
- Signatures made after the revocation, or without a timestamp, show as revoked.

## 6. Yearly chore

The revocation list is valid for one year. Department ▸ Open Authority ▸ **Renew Revocation List**, then
upload `authority.crl` again. The Department window says "Renew it soon" 30 days before.

## What it does not do

- A computer that never trusted the master (a stranger, a court clerk) sees "identity not verified". The
  signature is still mathematically valid, and the signing record (time, consent, computer details) still
  backs it up. Being trusted *everywhere automatically* needs a paid certificate from an Adobe-approved
  provider, which costs a yearly fee per person.
- This is not legal advice. For high-stakes documents, have counsel review the process.
