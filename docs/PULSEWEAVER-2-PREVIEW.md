# Pulse Weaver 2.0 preview

Version: **2.0.0-unstable.1**. Windows native interface and local-controls preview.

This build explores a clearer way to operate the existing studio: choose a category, open the task you need, and use Back to return. It retains the native Qt/OBS engine, Stage/Look system, landscape and portrait outputs, native Move, stingers, audio mixer and account connections. It is an unstable preview for testing before using it for an important broadcast.

## What changes

The Show workspace keeps programme previews and chat central, with Stage, destination and transport controls close to the work they affect. Provider choices retain their platform colors, neutral unselected choices and sliding selected state. Recording keeps its orange selected state and the separate **Default**, Off, 16:9, 9:16 and Both modes. Default continues using your native recording settings; it is not the same as routed 16:9 recording.

Primary navigation uses category cards and focused pages. Stages use a grid picker; commands use categories and action cards instead of one long command list. The existing native controllers remain behind these controls, including live-change restrictions. Advanced native tools remain reachable without filling the main operating surface.

Live-preview enablement and refresh rate remain local preview settings. They do not lower the broadcast frame rate. The compact mixer can be expanded when needed. This release does not promise a particular CPU or RAM reduction: compare it using the same show, sources, outputs and game as your regular build.

## Find the task

| Task | Workspace |
|---|---|
| Monitor the programme, choose a Stage, route destinations, record or go live | Show |
| Design and rehearse Looks and native movement | Control |
| Edit scenes, capture devices, sources and their properties | Camera |
| Connect accounts and edit stream details | Connections / Action |
| Configure sound, recording, canvas and application options | Settings category → task |
| Discover basic local commands and connection details | Commands → Stages, Sources, Audio, or Recording & replay |
| Open advanced OBS tools, docks, profiles, collections, updates and recovery | Studio |

Use visible Back controls to return without losing your configured values. Choosing a command card reveals its example; browsing examples does not run that command. Tables in these notes describe coverage, not a proposed long list in the application.

## Basic commands and Streamer.bot

The preview adds a small operating surface at `GET /api/v1/control` and `POST /api/v1/control`. It uses existing controls and existing source identities. It supports selecting/advancing Stages, scene-item visibility, source mute and volume, starting/stopping the selected recording mode, and saving an already-running replay buffer. It does not add a second encoder or require platform credentials.

Open **Commands** to copy the local endpoint, reveal/copy its masked connection token, browse categories and copy a JSON example. Replace placeholder source UUIDs and item IDs using **Copy source IDs & capabilities**. Tokens control this installation; keep them out of chat, screenshots and public exports.

A fresh preview defaults to local API port **18775**. A copied profile keeps its explicitly saved port, so always use the endpoint displayed by the instance you want to control. Regular Pulse Weaver defaults to 18755; Motion Preview defaults to 18765. Historical Builder Test installations also used 18775, so check availability before running several test editions at once. OBS WebSocket and any local overlay servers have their own ports.

The accompanying [Streamer.bot guide](../integrations/streamerbot/README.md) explains the C# example and private variables. This is a **paste-and-compile scaffold**, not an import-tested Streamer.bot package. Native command tests use inert controllers; actual Streamer.bot compilation and end-to-end operation need separate verification in your installed version. Read the example before attaching it to a live trigger.

Stage/source responses report observed state. Recording and replay requests can return HTTP 202 while native validation or file writing continues. An accepted request does not prove that recording started or that a finished file exists. Confirm the state in Pulse Weaver. Request IDs correlate replies; they do not provide automatic retries or duplicate suppression.

Existing Lumia routes and plugin command identities remain in place. To test Lumia against this separate instance, point its connection to this instance's endpoint/configuration and token. A fresh isolated instance does not inherit the regular instance's connection token automatically.

## Install and test separately

The preview installer is named `PulseWeaver-2-Preview-Setup-2.0.0-unstable.1.exe`. Its product is **Pulse Weaver 2 Preview** and its default directory is `%LOCALAPPDATA%\Programs\Pulse Weaver 2 Preview`. It has its own shortcut, uninstall registration, configuration, recovery backups and updater product identity. Installing this dedicated installer does not upgrade the regular Alpha installation. The ordinary unstable installer family does not provide this isolation.

Keep a fresh profile for the first check. The installer leaves automatic launch unchecked for the preview. Close other instances for a simple first run; if you deliberately run them together, ensure API, OBS WebSocket and overlay ports do not collide and verify which window each external client controls. Avoid streaming from both while comparing the interface.

If you later copy a show, close the instances first and use a separate copied configuration in the preview. Preserve the regular source configuration. Configuration isolation does not duplicate media files referenced by absolute path: editing or replacing a shared media file affects every show that uses that file. Do not copy a private profile into a public release payload or support archive.

A useful first pass is to open Show, choose a Stage card, check both programme views, expand the mixer, choose each destination format, confirm Default recording exists, and return through Control, Camera, Connections and Settings. Check that Back preserves your selections and that long labels fit at your usual Windows display scale. Test recording and playback locally before attempting a real stream. Verify stinger audio, Lumia alert audio and source exclusions using the same show you intend to broadcast.

Preview updates select only the dedicated **2 Preview** installer family. They cannot silently switch this instance to a generic regular, alpha or stable installer. **Studio → Updates → Restore a preview backup** opens this preview's recovery program. Uninstall **Pulse Weaver 2 Preview** to remove its local app/configuration; it has a different uninstall entry from regular Pulse Weaver. Keep a backup of any preview-only work you want to retain.

## Known limits

- This is an unstable interface preview. Native screenshots, layout checks and controller tests do not replace a real rehearsal with your own game, hardware, sources and connected services.
- Kick remains landscape-only. Provider restrictions, Twitch VOD track setup, audio monitoring and stage exclusions retain their existing meanings.
- Routed recording formats remain experimental; Default remains the native recording path. Stop recording before changing its mode.
- The basic command surface does not implement stream start/end, arbitrary code, source creation/deletion, source file/URL replacement or a new platform-event client. Advanced automation/Pulse AI availability is unchanged; a category or placeholder is not a completed integration.
- Recording/replay acceptance is asynchronous. Commands do not wait for completed files, and the example does not retry failed requests automatically.
- Imported settings can preserve old API/OBS WebSocket ports and browser sources. Running several editions can therefore create resource or port conflicts despite their separate configuration folders.

The [design research and parity contract](PULSEWEAVER-2-DESIGN-RESEARCH.md) records the source-backed rationale, retained controls and validation strategy.
