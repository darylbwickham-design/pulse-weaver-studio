# Altron Teleport for Pulse Weaver — native alpha 0.1.0

Included in the local Pulse Weaver 1.14.4-alpha.3 preview. No Lua script is needed for the native effect. The existing Lumia avatar plugin and privately shared overlay still supply the command centre, TTS/headphone glow, backgrounds and per-location camera placements. Do not share your Pulse Weaver config folder.

If using the smaller add-on ZIP instead of the full alpha app installer: extract the complete ZIP, close Pulse Weaver, and double-click **Install-Pulse-Weaver.cmd**. It copies only the native DLL and shader into the default per-user Pulse Weaver installation. It refuses overwriting an existing native add-on. The full alpha app installer is the update path.

## First setup

1. Install the alpha, then start Pulse Weaver. Enable its OBS WebSocket server and local Stage API as before.
2. Use a working camera cutout scene/source. Double-click `integrations/altron-teleport/bridge/Setup-Pulse-Weaver.cmd`, enter the cutout name and your alert source name. This creates NEW show/helper scenes and a native audio source, stopping if their names are already in use. It never edits your camera or Stage definitions.
3. In Manage stages, create a new `PW White Teleport` Stage and assign its landscape view to the new `PW White Teleport` scene. In the Lumia avatar plugin select Pulse Weaver Stage mode and SAVE.
4. Start the UPDATED `integrations/altron-teleport/bridge/Start-Bridge.cmd`. Only one bridge should run. Node.js 22+ remains required for this bridge; the native visual/audio effect itself does not need Node or Lua.
5. Import the privately supplied Lumia overlay and your existing Altron command routine. TEST the command, or call it from any Lumia alert/Lumi-Con key as before.

## Existing Altron proof-of-concept

Close the old bridge and use `integrations/altron-teleport/bridge/Use-Native-Teleport.cmd` after installing the alpha. It updates ONLY the dedicated `PW Teleport Actor` helper's teleport filter and adds audio to `PW White Teleport`. The previous Lua filter is preserved disabled as `White Ranger Teleport (Lua backup)`. Original cameras, transforms, backgrounds, alerts and Stage definitions remain untouched. Start the updated bridge afterwards. You can leave the old Lua loaded for other uses; native cues target this specific filter directly.

## Choose your own sounds

In the dedicated show scene, open **Altron Teleport Audio → Properties**.

- Browse for separate arrival and departure files (WAV, MP3, OGG, FLAC or M4A). Files stay on your PC; blank/missing files mean a silent effect.
- Set sound volume and separate delays from 0–1500 ms after the visual cue.
- Sound goes through the app's audio mixer and stream/recording tracks. To hear it locally, enable the monitoring checkbox and select your headphones in **Settings → Audio → Monitoring Device**. This uses the app's shared monitoring device; it does not replace it or provide a separate per-source device selector.
- Put the dedicated show Stage live, then use **Test arrival sound / Test departure sound**, or **Test arrival / Test departure** in the actor's filter properties. Reset cancels queued sounds and stops playback. Already buffered audio can take a brief mixer interval to drain.

Arrival/departure animation automatically triggers the matching sound. One Lumi-Con action triggers both. The existing bridge's Enter/Reset actions show the camera normally; only animated In/Out cues play sounds.

## Compatibility and testing

The existing GPU shader is reused, with a separate clock per filter. The updated bridge detects native versus Lua filters and preserves the Lua fallback for older installs. Native DLL compatibility was checked with the recipient runtimes listed in VALIDATION.txt; other versions/platforms require testing or a matching build. This preview does not change existing Stage settings automatically and is not yet a published channel update.
