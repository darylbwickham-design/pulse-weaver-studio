# Pulse Weaver Mac Preview

Separate experimental Apple Silicon branch: `codex/macos-apple-silicon`.
Mac versions use `mac-v0.x.x-alpha.N` tags. Windows main remains on 1.12.1.

Read [Mac installation and testing instructions](packaging/macos/README-MAC.txt).
The Mac build uses an isolated app identity and settings directory, macOS
Keychain for app secrets, and disables OBS updates and virtual camera extensions.

The Mac GitHub Actions job builds and checks the actual arm64 application on
macOS before publishing a DMG, app ZIP, source archive and SHA256 checksums.
A failed build does not publish a usable-release claim or installer.

## Build on an Apple Silicon Mac

Install Xcode 26.6 and CMake 3.28 or newer, then run:

```bash
bash packaging/macos/build-preview.sh
```

Minimum target OS is macOS 13. The build requires a newer SDK, not a newer
OS on the tester's Mac. Hardware capture and live streaming still require testing.
