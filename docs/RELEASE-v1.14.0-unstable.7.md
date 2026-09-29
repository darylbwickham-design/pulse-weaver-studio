# Pulse Weaver 1.14.0 unstable 7

This preview clarifies how Control's saved Looks relate to on-air Stage transitions.

## Control changes

- **Preview from** now lets you choose another saved Look in the same Stage before playing the destination Look's movement. The offline preview uses that Look's saved source positions on both canvases. The current scene state remains available as a starting point.
- When the chosen start and destination placements already match, Control explains why no visible movement occurs instead of silently playing an empty preview.
- The page now explains that a Stage switch uses its fade, cut or stinger and reveals the prepared Look, while two Looks within the same Stage animate source placement.
- **Run saved look live** names the on-air action clearly. Selecting, editing, saving and offline previewing remain separate actions.
- Saved preview positions use source IDs and the same resize adjustment as live execution, so source renames and camera resolution changes do not redirect the preview.

Unstable 6's audio routing correction and the guided show setup remain included. No existing scenes, credentials, Lumia port or installed Pulse Weaver instance were changed.

## Verification and limits

The native motion plugin was compiled and the installer self-checks were run. The updater was checked against live release metadata. The packaged Control page has not received a full visual click-through or live broadcast test; users should rehearse this experimental build off air before relying on its preview.
