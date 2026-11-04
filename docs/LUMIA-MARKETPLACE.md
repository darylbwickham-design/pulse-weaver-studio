# PulseWeaver Stream Studio

## Short description

Connect Lumia Stream and LumiCon to Pulse Weaver. Switch Stages and Looks, control streams, recording and audio, and trigger Lumia reactions from Pulse Weaver's 15-button keypad.

## Full description

Control your Pulse Weaver show from Lumia Stream reactions, commands and LumiCon. Switch an existing Stage, run a saved Look or motion action, and return from a temporary presentation with Undo.

Operate your configured Twitch, Kick and YouTube destinations, start or stop recording, control existing scene-item visibility, adjust source mute and volume, and operate supported media playback.

Turn Pulse Weaver's chat panel into an on-screen keypad with up to **15 named buttons**, using included icons or your own images. Each button triggers its matching **Keypad 01–Keypad 15** alert in Lumia, where you choose its reactions. Automatic alerts can also respond to Stage, source, media, transition and output changes. **All alerts default to Off** so you choose which reactions to enable. The **Open Pulse Weaver Manual** action opens a bundled, printable offline book while the Lumia integration is connected; the bundled file can also be opened directly.

Requires **Pulse Weaver 2.0 for Windows** for the full feature set, running on the same computer as Lumia Stream. Tested with **Lumia Stream 9.3.4**; the manifest declares Lumia 9.x compatibility. Create your show, connect destinations and configure audio routing in Pulse Weaver first. This is a companion integration, not a standalone streaming application.

The standard installation connects locally with automatic token detection. Custom installations can specify a configuration file and port. No OBS WebSocket connection is needed. The plugin reconnects when Pulse Weaver reopens without replaying old button presses or alerts.

Previously named **Pulse Weaver Motion Preview**. The permanent plugin ID remains **`pulseweavermotionpreview`**, preserving existing action and alert bindings, saved field keys and variable names.

## Links

- Website and Pulse Weaver download: [lumi-con.com/pulse-weaver](https://lumi-con.com/pulse-weaver/)
- Tutorial: upload the accompanying **PulseWeaver-Lumia-Tutorial.md** with this listing. The plugin package also includes the printable manual.
- Source code and issues: [pulse-weaver-studio on GitHub](https://github.com/darylbwickham-design/pulse-weaver-studio)
- Support and contact: [Pulse Weaver Discord](https://discord.gg/MQ6pAN9Q)
- Licence: **MIT**.

## Submission notes

- Upload **PulseWeaver-Lumia-1.5.2.lumiaplugin**. Use the PNG icon provided alongside it if the submission form asks for one.
- Keep the manifest ID **`pulseweavermotionpreview`**. Do not register a new identity because the display name changed.
- Version **1.5.2** adds the offline manual and a local open-manual action. Existing action/alert IDs and alert defaults are unchanged.
- The package has passed Lumia's official manifest validator and the existing 30-test plugin suite. This does not claim that marketplace review has already approved it or that a fresh-profile installation was performed.
- A new-user smoke test can use **Activate Stage** and one enabled keypad alert. It does not require starting a broadcast.
