# PDF Fire — Code signing policy

Free code signing provided by [SignPath.io](https://about.signpath.io), certificate by [SignPath Foundation](https://signpath.org).

## What is signed

Only the Windows installers and programs of PDF Fire that are built from this repository
(<https://github.com/firehouse360/PDF-Fire>) by its automated build on GitHub Actions
([`.github/workflows/pdf-fire-windows.yml`](../.github/workflows/pdf-fire-windows.yml)). Nothing built on a
personal computer, and no third-party program, is signed with this certificate.

The Linux packages (`.deb`, `.rpm`) and the update repository are signed separately with the PDF Fire release
key, fingerprint `1AD3 AB56 3A5F C1D4 F403 632A C011 9E5C 2237 8B81`.

## Team roles

| Role | Members |
|---|---|
| Committers and reviewers | [firehouse360](https://github.com/firehouse360) (Firehouse 360) |
| Approvers | [firehouse360](https://github.com/firehouse360) (Firehouse 360) |

- **Committers** may change the source code in this repository. Changes are developed with AI-assisted coding
  and are reviewed, built and tested before they are merged.
- **Approvers** approve every signing request; a release is signed only after an approver has checked that it
  was built from this repository by the automated build.
- Every team member uses multi-factor authentication for GitHub and SignPath.

## Privacy

This program will not transfer any information to other networked systems unless specifically requested by the
user. See [PRIVACY.md](PRIVACY.md) for the features that use the network when the user asks for them.
