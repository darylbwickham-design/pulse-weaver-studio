# GitHub updates

Studio → Updates offers a manual check and a daily automatic-check preference.
Checks read public GitHub Releases without a GitHub token. Failed background
checks wait at least an hour before retrying. Automatic checks never install an
update by themselves. The upstream OBS/Sparkle updater remains disabled.

The installer writes `bin/64bit/pulseweaver-update.json` with schema 1, the
installation channel and the installed release tag. The Mac package writes the
same file in `Contents/MacOS` before signing. Development and portable copies
without this identity do not guess an installation channel.

| Channel | Release tag | Required asset |
| --- | --- | --- |
| windows-public | v1.12.6 | PulseWeaver-Public-Dist-1.12.6-Setup.exe |
| windows-private | v1.12.15 | PulseWeaver-Setup-1.12.15-BETA.exe |
| mac-arm64-preview | mac-v0.1.0-alpha.2 | PulseWeaver-Mac-mac-v0.1.0-alpha.2-AppleSilicon.dmg |

Windows 1.12.2 and Mac alpha 2 introduce the updater. Windows installers use
their Program.Version constant; Mac uses PULSE_MAC_TAG. Update both installer
project/payload versions when making the next Windows release. Increment the Mac
workflow's tag before publishing its next build; never replace a released tag.

Only published, non-draft releases with an exact matching asset, positive bounded
size and GitHub's `sha256:` asset digest qualify. Prereleases are intentional in
these preview channels. Version components and Mac alpha numbers compare
numerically. A newer Mac preview cannot supersede Windows, and the private
Windows channel never falls back to the public installer.

Downloads use HTTPS, follow only GitHub/CDN redirects, stream to a temporary file
and verify both length and SHA-256 before opening anything. Cancellation, missing
digests, network errors and verification failures leave the installation alone.
All OBS outputs (including provider outputs) must be stopped before offering the
update and again before installer handoff. Windows opens the normal setup UI and
closes the app; setup still refuses to overwrite a running Pulse Weaver process.
Mac opens the verified DMG and closes the app; the user drags the replacement app
to Applications. This is not an unattended Mac app replacement.

Windows setup preserves the config folder; Mac settings remain in the separate
Library application-support folder. Successfully opened installer downloads are
left in the user's temporary folder so setup can finish after the app exits.

## Release/bootstrap requirements

Existing 1.12.1 and Mac alpha 1 installations need one manual installation of a
new build containing the updater. They cannot acquire this feature retroactively.
Each release must include the matching installer for every supported channel. When shipping
the next private build, upload its credential-free installer under the exact
private asset name after the normal credential audit. Never publish configuration,
app registration secrets, account tokens or the private handoff file. Missing
private assets result in no eligible private update, never a cross-installation.
Use GitHub Releases' list endpoint, not `/latest`, because Mac/Windows previews
share a repository and are separate release lines.

## Validation

`tests/Updates` is a Qt6 Core/Network/Widgets CMake target. Its regression checks
cover mixed release channels, numeric versions/alpha order, drafts, downgrades,
missing/bad digests, invalid asset sizes and names, foreign URLs and redirect
restrictions. An optional first argument points to the packaged Qt plugin folder
to verify TLS availability; an optional second argument supplies captured GitHub
release JSON for actual-asset discovery checks. The Mac packaging script builds
and runs the test with its bundled plugins before signing.

Both Windows setup `/test` modes validate written channel/version metadata and
exercise preservation of configuration while replacing obsolete payload files.
