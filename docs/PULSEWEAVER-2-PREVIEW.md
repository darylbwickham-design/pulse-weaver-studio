# Pulse Weaver 2.0 preview

Version: **2.0.0-unstable.4**. Windows native interface and local-controls preview.

This build brings the main workspaces forward, gives programme previews more room and uses a consistent tactile style for operating controls. Within each task, choose a category, open its focused page and use Back to return. It retains the native Qt/OBS engine, Stage/Look system, landscape and portrait outputs, native Move, stingers, audio mixer and account connections. It is an unstable preview for testing before using it for an important broadcast.

## What changes

This revision rebuilds **Build my show** into five click-through phases: Start, Sections, Looks, Sources and Review. Previewing a Look does not include it. Configure opens a focused editor, transitions have separate landscape/portrait tabs, and optional shared layers can be deferred. Review shows actual assignments and lets you return directly to their source questions. The wizard uses the main UI's raised navy/cyan controls, with wrapping and visible actions checked at three window sizes. See [Create your show](SETUP-WIZARD-GUIDE.md).

The Show output/status and broadcast controls now sit above the programme previews and mixer. The compact mixer retains its content-based expanded height, and the unused portrait bitrate control for landscape-only Kick stays hidden.

The four main workspace controls—**Show**, **Lights**, **Camera** and **Action**—stay visible as large buttons. The Show operating row combines Stage, capture and Show/Control choices; it removes the separate heading/control block so programme previews get more of a small display. Buttons share the sliders' raised cap, highlight and pressed feedback instead of mixing flat navigation with tactile sliders.

Provider choices retain their platform colors, neutral unselected choices and sliding selected state. Recording keeps its orange selected state and the separate **Default**, Off, 16:9, 9:16 and Both modes. Default continues using your native recording settings; it is not the same as routed 16:9 recording.

Setup, Stages, Commands and Lights use category/device cards and focused pages instead of long default pickers. The existing native controllers remain behind these controls, including live-change restrictions. Advanced native tools remain reachable without filling the main operating surface.

Live-preview enablement and refresh rate remain local preview settings. They do not lower the broadcast frame rate. The compact mixer expands to fit its native channel controls and reserves space for horizontal scrolling. Oversized saved splitter layouts are clamped so unused height returns to the programme previews. This release does not promise a particular CPU or RAM reduction: compare it using the same show, sources, outputs and game as your regular build.

## Find the task

| Task | Workspace |
|---|---|
| Monitor the programme, choose a Stage, route destinations, record or go live | Show |
| Design and rehearse Looks and native movement | Control |
| Choose a lighting device, adjust its local controls or load a saved Look | Lights |
| Edit scenes, capture devices, sources and their properties | Camera |
| Connect accounts and edit stream details | Connections / Action |
| Configure sound, recording, canvas and application options | Settings category → task |
| Discover basic local commands and connection details | Commands → Stages, Sources, Audio, or Recording & replay |
| Open advanced OBS tools, docks, profiles, collections, updates and recovery | Studio |

Use visible Back controls to return without losing your configured values. Choosing a command card reveals its example; browsing examples does not run that command. Tables in these notes describe coverage, not a proposed long list in the application.

## Lights

Lights exposes the retained device setup, power, brightness, color and saved-Look controls. Choose a device card to open its controls; use Back to return. Saved-Look cards load values into the local controls. Sending those values to a device is a separate explicit action. Device setup retains the provider and connection settings used by the existing implementation.

The provider paths cover WLED, Home Assistant, legacy Philips Hue light state and compatible HTTP/DIY endpoints. Request-format checks and synthetic navigation tests do not prove compatibility with your physical devices. A connection test that reaches a port is not proof that credentials, the provider API or the light itself work.

Hue requests use HTTPS and normal certificate validation. This preview does not automatically pair a bridge, provision its trust certificate or silently accept a failed certificate/name check. A bridge addressed by raw IP may require appropriate trusted endpoint/certificate setup before requests work. No real light discovery, test or Apply action is performed by the layout audit; hardware validation remains a separate step.

## Basic commands and Streamer.bot

The preview adds a small operating surface at `GET /api/v1/control` and `POST /api/v1/control`. It uses existing controls and existing source identities. It supports selecting/advancing Stages, scene-item visibility, source mute and volume, starting/stopping the selected recording mode, and saving an already-running replay buffer. It does not add a second encoder or require platform credentials.

Open **Commands** to copy the local endpoint, reveal/copy its masked connection token, browse categories and copy a JSON example. Replace placeholder source UUIDs and item IDs using **Copy source IDs & capabilities**. Tokens control this installation; keep them out of chat, screenshots and public exports.

A fresh preview defaults to local API port **18775**. A copied profile keeps its explicitly saved port, so always use the endpoint displayed by the instance you want to control. Regular Pulse Weaver defaults to 18755; Motion Preview defaults to 18765. Historical Builder Test installations also used 18775, so check availability before running several test editions at once. OBS WebSocket and any local overlay servers have their own ports.

The accompanying [Streamer.bot guide](../integrations/streamerbot/README.md) explains the C# example and private variables. This is a **paste-and-compile scaffold**, not an import-tested Streamer.bot package. Command validation includes an isolated native studio with inert sources; actual Streamer.bot compilation and end-to-end operation need separate verification in your installed version. Read the example before attaching it to a live trigger.

Stage/source responses report observed state. Recording and replay requests can return HTTP 202 while native validation or file writing continues. An accepted request does not prove that recording started or that a finished file exists. Confirm the state in Pulse Weaver. Request IDs correlate replies; they do not provide automatic retries or duplicate suppression.

Existing Lumia routes and plugin command identities remain in place. To test Lumia against this separate instance, point its connection to this instance's endpoint/configuration and token. A fresh isolated instance does not inherit the regular instance's connection token automatically.

## Install and test separately

The preview installer is named `PulseWeaver-2-Preview-Setup-2.0.0-unstable.4.exe`. Its product is **Pulse Weaver 2 Preview** and its default directory is `%LOCALAPPDATA%\Programs\Pulse Weaver 2 Preview`. It has its own shortcut, uninstall registration, configuration, recovery backups and updater product identity. Installing this dedicated installer does not upgrade the regular Alpha installation. The ordinary unstable installer family does not provide this isolation. Installing this revision updates an existing **2 Preview** instance and preserves that instance's configuration.

Keep a fresh profile for the first check. The installer leaves automatic launch unchecked for the preview. Close other instances for a simple first run; if you deliberately run them together, ensure API, OBS WebSocket and overlay ports do not collide and verify which window each external client controls. Avoid streaming from both while comparing the interface.

If you later copy a show, close the instances first and use a separate copied configuration in the preview. Preserve the regular source configuration. Configuration isolation does not duplicate media files referenced by absolute path: editing or replacing a shared media file affects every show that uses that file. Do not copy a private profile into a public release payload or support archive.

A useful first pass is to open Show, choose a Stage card, check both programme views, expand the mixer, choose each destination format, confirm Default recording exists, and return through Control, Camera, Connections and Settings. Check that Back preserves your selections and that long labels fit at your usual Windows display scale. Test recording and playback locally before attempting a real stream. Verify stinger audio, Lumia alert audio and source exclusions using the same show you intend to broadcast.

Preview updates select only the dedicated **2 Preview** installer family. They cannot silently switch this instance to a generic regular, alpha or stable installer. **Studio → Updates → Restore a preview backup** opens this preview's recovery program. Uninstall **Pulse Weaver 2 Preview** to remove its local app/configuration; it has a different uninstall entry from regular Pulse Weaver. Keep a backup of any preview-only work you want to retain.

## Known limits

- At the minimum window height, expanding the compact mixer reduces the preview viewport by the height required for its channel controls. Use a taller window or open Full mixer when you need more room for both.
- This is an unstable interface preview. Native screenshots, layout checks and controller tests do not replace a real rehearsal with your own game, hardware, sources and connected services.
- Kick remains landscape-only. Provider restrictions, Twitch VOD track setup, audio monitoring and stage exclusions retain their existing meanings.
- Routed recording formats remain experimental; Default remains the native recording path. Stop recording before changing its mode.
- Lighting controls are restored, but physical-device behavior, Hue bridge trust and provider-specific capabilities still require a separate check with your hardware. Selecting a saved Look loads local controls; it does not send an Apply command automatically.
- The basic command surface does not implement stream start/end, arbitrary code, source creation/deletion, source file/URL replacement or a new platform-event client. Advanced automation/Pulse AI availability is unchanged; a category or placeholder is not a completed integration.
- Recording/replay acceptance is asynchronous. Commands do not wait for completed files, and the example does not retry failed requests automatically.
- Imported settings can preserve old API/OBS WebSocket ports and browser sources. Running several editions can therefore create resource or port conflicts despite their separate configuration folders.

The [design research and parity contract](PULSEWEAVER-2-DESIGN-RESEARCH.md) records the source-backed rationale, retained controls and validation strategy.
