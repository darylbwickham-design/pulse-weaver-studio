# Altron Command Centre — included in Pulse Weaver 1.14.4-alpha.4

The command controller, smooth white scribble teleport filter and sound player are built into this Pulse Weaver app update. They start and stop with the app. No separate bridge, Node installation, Lua script or WebSocket/Stage API setup is required for Altron.

## First setup

1. Install the full alpha.4 app update and open Pulse Weaver. If the previous standalone Altron bridge is running, close its window once so the built-in controller can use port 18767.
2. In the privately supplied Lumia avatar plugin, choose **Pulse Weaver Stage** mode and SAVE. Keep the private Lumia overlay and existing Altron command routine.
3. Open **Studio → Tools → Altron Command Centre**. Choose your working background-removed camera scene/source and alert layer, then click **Create / use Altron show**. It creates the dedicated show, helper scenes, audio source and a new `PW White Teleport` Stage. Existing Stage entries and original camera transforms are preserved. Stop active streams/recordings before setup.
4. For the existing proof of concept, the same button upgrades only its dedicated Altron helpers. The old Lua filter is retained disabled as a backup; other uses of Lua remain unchanged.
5. TEST the existing **!altron-show** command in Lumia, or call it from any Lumia alert or Lumi-Con key. The app's dialog also has Enter, Teleport in, Teleport out and Reset buttons for quick tests. Lumia must be open with the avatar plugin running.

The single show scene stays selected while Lumia changes locations. Destination images, Altron's head placement and camera placements remain configurable in the native Lumia overlay/plugin settings. The head follows the actual camera transform, including movement, scaling, rotation and cropping. The show uses the existing camera cutout as a scene reference rather than opening the hardware camera again.

## Your own sound effects

Click **Choose arrival / departure sounds** in the command-centre dialog, or open **Altron Teleport Audio → Properties** in the dedicated show.

- Choose separate arrival/departure files (WAV, MP3, OGG, FLAC or M4A), volume and delays from 0–1500 ms. Blank or missing files produce a silent teleport.
- Sound plays through Pulse Weaver's mixer and assigned recording/stream tracks. To hear it locally, enable monitoring and select headphones in **Settings → Audio → Monitoring Device**. This is the app's shared monitoring device.
- Arrival/departure cues trigger sound automatically. Reset cancels pending cues and playback. A small amount of already buffered audio may drain afterwards.

The Lumia headphone-output monitor and TTS routing remain in the avatar plugin. They drive the talking head independently of the native teleport sound player.

## Troubleshooting

- **Port unavailable:** close the previous standalone bridge once. Pulse Weaver retries automatically; subsequent app starts need no bridge launcher.
- **Lumia unavailable:** open Lumia and enable the private avatar plugin. The built-in controller uses its registered local overlay port (8766 by default).
- **Helper or Stage name conflict:** setup stops without overwriting unrelated sources or Stage assignments. Use the dedicated Altron names configured in Lumia.
- **Camera cutout missing:** select a working background-removal scene/source. This update reuses your cutout; it does not install a new background-removal model.

This is a local Windows x64 alpha preview, not a published updater release. See VALIDATION.txt for tested behavior. Private overlays, avatar artwork, account settings and user configurations are excluded from the public native source package.
