# Installer backup and recovery regression

`dotnet run --project tests/InstallerRecovery/InstallerRecovery.csproj -c Release`

The console harness links the production recovery engine and writes only unique disposable temporary directories. It covers full backup/restore, version and config restoration, removal of newer-only files, exclusive locking, injected failures, interrupted replacement, unsafe archive names and corrupt backup refusal.

Both private installer EXEs also support `/test`, `/migration-test`, `/language-test` and `/layout-test`. Their `/upgrade-test <old-runtime-zip> [config-directory] [recovery-exe]` command creates a temporary original-runtime installation, privately copies configuration, applies the embedded update and restores the saved backup. When a recovery EXE is supplied, that separate binary performs the restoration. No registry, shortcuts or installed app files are written. Only a counts/status report is written beside the test EXE. Treat any configuration input as private and never package it.

The internal `/restore-test` mode refuses destinations outside a generated `PulseWeaver-upgrade-proof-*/Studio` temporary directory. These are verification interfaces, not unattended installation flags.
