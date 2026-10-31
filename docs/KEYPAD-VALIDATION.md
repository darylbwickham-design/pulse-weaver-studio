# Chat keypad and Lumia 1.5.0 validation

Candidate: Pulse Weaver 2.0.0 unstable.11, Windows x64. Checked 31 October 2026.

The Show / Control selector uses inline text with a blue selected word. The Lights tab remains visible, with Devices, Live control and Saved looks panels showing “Coming soon” in place of their working controls. Action's Alerts tab and Camera's Overlay designer entry are hidden; their implementations and saved content remain in place.

The chat card switches to a maximum of 15 configurable square keys in three columns and five rows. Each key keeps a fixed numbered Lumia alert, independently of its editable name and icon. Twelve built-in icons and imported images are supported. Imported images are copied beside the UI settings, decoded at a bounded size and cached. No keypad idle timer or polling connection was added.

## Checks completed

- Native frontend and core built successfully with Visual Studio 2022 / Qt 6.11.1.
- `tests/Keypad`: 225 checks passed for all 15 keys, widths 246–420 pixels, square sizing, three-column layout, cancel/rename persistence, edit safety, bounded image loading, legacy subscriber compatibility, offline rejection and local event delivery.
- `tests/LumiaPlugin.test.cjs`: 29 tests passed, including all 15 alert mappings, stale/duplicate/invalid event rejection, reconnect without replay, unload guards, disabled-alert reporting and regressions for existing Stage/Look/output/source/audio/media controls.
- Lumia's official `lumia-plugin` 1.0.0 CLI validated the source and extracted final package and built the standalone `.lumiaplugin` artifact.
- Compared the new manifest with unstable.10: manifest ID `pulseweavercontrol`, all 17 existing action definitions and all 36 existing automatic alert definitions are preserved. There are 15 added keypad alerts, for 51 total.
- Native click-through in an isolated installation: observed all 15 keys in five rows; Chat round-trip preserved preview/card bounds; active Control text turned blue; Alerts and Overlay designer entry points were absent.
- Final Lights check: the main navigation tab remains, its normal heading stays at the top, and three empty Devices / Live control / Saved looks panels display “Coming soon”. Working lighting controls are hidden.
- Pressed key 15 in the native app and observed exactly `keypad_15`, slot 15 and its configured name in the plugin's mock Lumia SDK receiver over the real authenticated local event stream.
- Imported a custom PNG through the native file chooser and saved it: the copied PNG rendered on key 01, and editing produced no additional Lumia alert.
- Installer maintenance, language and layout self-tests all exited 0. Packaged frontend, core and theme hashes match the visually tested instance.

The isolated test used inert sources, no platform accounts and a separate local API port. Real broadcasts and real Lumia reactions were not fired. The installed user instance was left running.

## Packages

- App: `PulseWeaver-Setup-2.0.0-unstable.11.exe`.
- Plugin: `PulseWeaver-Lumia-1.5.0.lumiaplugin`, displayed as **PulseWeaver Stream Studio**.
- New keys are named **Keypad 01** through **Keypad 15**. Each reaction is configured in Lumia. The Pulse Weaver message “Sent to Lumia” confirms local hand-off rather than completion of that reaction.

These are local distribution candidates. This validation does not represent Lumia marketplace approval.
