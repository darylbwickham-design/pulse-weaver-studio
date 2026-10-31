# PulseWeaver Stream Studio — 1.5.0

The Lumia companion plugin for Pulse Weaver streaming software. Use Lumia Stream reactions and LumiCon to switch Stages and Looks, control outputs, sources, audio and media, and run Lumia alerts from Pulse Weaver's on-screen keypad.

Import **PulseWeaver-Lumia-1.5.0.lumiaplugin** over your existing plugin. Its manifest ID stays **`pulseweavercontrol`**; existing actions, alerts, field keys and variable names remain compatible. Keep your current port and credentials. See [setup](SETUP.md), [actions](ACTIONS.md) and [changes](CHANGELOG.md).

## On-screen keypad

In Pulse Weaver, select the keypad icon beside **Chat**, then **Add button**. Give it a short name and choose one of 12 included icons or your own image. The panel holds up to 15 square buttons in three columns; their side length follows the available width, capped at 112 pixels. Chat and previews keep their existing space.

In Lumia Stream, find **PulseWeaver Stream Studio → Keypad 01** (through **Keypad 15**) and add the reaction you want, such as selecting a Stage or running a Look. The button editor shows its numbered alert. Renaming or changing the icon keeps that alert number. **Edit buttons** lets you configure keys without firing their reactions.

Keypad alerts default to On because they require an explicit button press. Enable a previously disabled keypad alert in Lumia if necessary. They require a Pulse Weaver build with the keypad bridge (2.0 unstable.11 or later). Existing plugin controls continue to work on older compatible builds. A disconnected press is rejected and never replayed later. “Sent to Lumia” confirms the local event was handed off; it does not confirm that a Lumia reaction or animation has completed.

Custom icons are copied into the installation's `keypad-icons` folder, resized to at most 256 × 256, and cached while the panel is open. There is no new idle polling.

## Stages and Looks

**Undo Previous Stage / Look** is the single restore action in the menu. Its existing `restore_motion` ID is unchanged, so current Lumia and LumiCon Undo bindings remain attached. The separate Return, Stop and Restore, and Restore Original Scenes actions are no longer offered. Older saved bindings retain their handlers for compatibility.

Stage and look actions remember the preceding presentation automatically. Undo returns to the captured stage from any current stage and stops an unfinished motion first. It retains the previous stage's camera layout and restores the outgoing motion's baseline when that execution still owns it. A single temporary same-stage look restores its native baseline. Several same-stage changes require a previously observed saved look; otherwise Undo reports that the exact earlier layout is unavailable.

For several temporary actions, add **Remember Current Stage / Look** first to pin the starting destination until Undo succeeds. `previous_stage` and `previous_look` expose it. Memory clears on plugin unload/settings changes, and a successful Undo consumes it. With no remembered destination, Undo retains the native layout-only fallback. This is one return point rather than a history of manual scene edits.

For the newsroom at default timings: Run News intro, Delay 1.5 seconds, Send Custom Overlay Content, Delay 33.8 seconds, Undo Previous Stage / Look. The Undo action goes at the end of the chain or under End of Command after the alert duration.

The plugin cancels queued controls and ignores late state/event replies after unload or connection-setting changes. In-place alpha upgrades use the same installation and connection. Motion controls are available when the connected build supports them.

The motion catalogue includes named stage looks such as Game, Chatting, Printer and BRB. Run Stage Look / Motion Action triggers each look from Lumia reactions or LumiCon buttons. Undo is the single restore control exposed to Lumia.

## Operating controls

- Start/end the whole configured show, or start/stop Twitch, Kick and YouTube individually.
- Activate an existing Stage, next Stage or previous Stage.
- Run a named stage look or motion action, remember the starting presentation, and undo the previous stage/look change.
- Show/hide/toggle an existing scene item.
- Mute/unmute/toggle and set volume (0–100%) on existing audio sources.
- Play, pause, restart, stop, next and previous on existing controllable media sources.
- Start/stop the recording mode already selected in Pulse Weaver.
- Reconnect after opening Pulse Weaver or changing its connection settings.

Platform start uses the saved horizontal/vertical/dual mode. A platform set to Off must be configured in Pulse Weaver first. Source mute and volume affect that source everywhere it is used; they do not edit platform audio exclusions. Visibility selects a specific scene/group item. Generated private routing copies are not exposed as editable scene items.

There are **no** scene/source creation or deletion actions, transforms, filters, file/URL replacement, configuration editing, or raw request actions. Lumia-labelled requests to the general editing API are rejected. The connection token is still the local API token, not a separately sandboxed credential; do not distribute it.

## Alerts and feedback

The 36 automatic state alerts default to **Off** in Lumia. Enable individual alerts when you configure their reactions. The 15 operator-triggered **Keypad 01–15** alerts default to **On**. Lumia preserves existing saved alert switches during an update. No additional plugin master switch is required.

The 51 alert definitions include the 15 keypad keys plus Stage changes, source shown/hidden/muted/unmuted/volume, media playback, transition begin/end, and platform/recording starting/live/stopping/stopped/failed (plus platform reconnecting). Output alerts include the specific output: a dual YouTube or recording route can produce separate output events. A platform's status confirms both YouTube outputs for Dual; partial failure is reported as failure rather than full success.

Existing status variables remain, with motion, Twitch, Kick and YouTube status variables. Action return variables keep Lumia's existing `pulseweavercontrol_` prefix. A start command only reports success after live state is observed; a timeout stops the Lumia action chain and tells you to check the app. A failed whole-show start may leave successful destinations running, and its error says so.

Events arrive over one authenticated local event stream. There is no idle status polling. Source lists update when their catalogue changes, variables/options update only when changed, and the connection sends a small heartbeat every 20 seconds while subscribed. Reconnection retries back off to once every 30 seconds while Pulse Weaver is closed for an upgrade or rollback. Automatically discovered tokens are reread on reconnect; explicitly supplied credentials remain unchanged. Reconnecting refreshes the current state without replaying historical alerts.

## Connection

Port 18755 remains the default for the normal Pulse Weaver installation. The separate **Pulse Weaver Motion Preview** package uses manifest ID `pulseweavermotionpreview` and port 18765, so it can coexist with the release plugin. Build it with `packaging/Build-LumiaMotionPreview.ps1`. Its variables and action results use the `pulseweavermotionpreview_` prefix. Token discovery stays within the selected installation; it does not fall back to the other profile. For a custom portable installation, set the full `config/obs-studio/plugin_config/pulse-weaver-core/pulse-weaver.ini` path or enter its connection token. The task-specific package includes its configuration path, never its token. The 1.3.1 preview package routes Restore Last Motion Layout separately from Stop and Restore Motion.

Saved look requests are asynchronous: acceptance means the cue was queued, not that the animation has finished. A Stage change preloads the saved layout before the configured stinger reveals it. Within the same Stage, looks animate directly. Keep your Chatty layout actions in the same Lumia reaction; Pulse Weaver preserves Chatty as a locked full-canvas overlay.

Do not use Lumia's generic OBS connection to bypass these operating controls. Build scenes, Stages and routing inside Pulse Weaver.

## Verification

The Node test suite exercises pushed events, dynamic lists, platform isolation, completion/failure handling and rejection of unsupported actions. The native smoke test runs against an isolated development runtime, operates existing test sources, verifies their events and checks forbidden routes. No live platform broadcasts are started by these tests.
