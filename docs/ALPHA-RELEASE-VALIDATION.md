# Alpha upgrade validation — 25 September 2026

Built a release maintenance runtime from `codex/release-alpha-updates` and a Motion runtime from `codex/alpha`, in separate build directories. The alpha distribution was configured with `PULSEWEAVER_MOTION_PREVIEW=OFF`, retaining the regular default API port. The active isolated preview and normal installation were not replaced.

Prepared installers:

- `PulseWeaver-Setup-1.12.16-BETA.exe`, channel `windows-private` (the existing regular release channel).
- `PulseWeaver-Setup-1.13.0-alpha.1.exe`, channel `windows-alpha`.
- `PulseWeaver-Lumia-1.4.0.lumiaplugin`, manifest ID `pulseweavercontrol`, identical package in both downloads.

Validation completed:

- Native release and alpha builds succeeded.
- Updater tests passed: release defaults, alpha opt-in, numeric alpha revisions, final-release ordering, wrong-target rejection, digest/URL validation, and clean installer handoff.
- Recovery tests passed: complete backup and restore, interrupted replacement, corruption rejection, older-alpha downgrade protection, and retained custom Lumia port/token.
- All 13 Lumia tests passed, including extended reconnect downtime and custom installation identity. All 14 pre-existing release action types and their field keys are retained.
- Both actual installer EXEs passed `/test`, `/migration-test`, `/language-test`, and `/layout-test`.
- Actual packaged round trip: **1.12.16 → 1.13.0-alpha.1 → full restore using the separate 1.12.16 installer**. In a disposable installation, all **1,433 copied configuration files and 3,592 total files** were restored byte for byte. The temporary input profile copy was removed after the successful test.
- Each runtime payload contains 2,176 archive entries, with no personal configuration, logs, motion actions, or debug files. YouTube desktop registration is included separately from user credentials.

Installer SHA-256:

```
A3B4D750825B942EB0E9DEA48D957AE4F7EF05A0B90AC5892CB101322DF7E94D  PulseWeaver-Setup-1.12.16-BETA.exe
86ED5D0BFC6E39EB19EB8A9536A0AE8C715293DD43262AD127D8A0118799D2B6  PulseWeaver-Setup-1.13.0-alpha.1.exe
06695313D936158AA535B6E1FBA696EFCD0A26FE8F108A31E3AACAD5EDA41DFF  PulseWeaver-Lumia-1.4.0.lumiaplugin
```

These checks did not start broadcasts, install over the user's app, or authenticate with streaming providers. External media files referenced by scenes are outside the installation backup. The older separate Public Preview installation is a different target and is not silently migrated into the regular installation.
