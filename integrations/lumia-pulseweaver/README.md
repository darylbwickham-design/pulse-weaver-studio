# Pulse Weaver for Lumia Stream — 1.2.1

Version 1.2.1 updates the existing Pulse Weaver Lumia plugin. It keeps the `pulseweavercontrol` ID so current Lumia reactions and LumiCon alert bindings remain attached to the same plugin. Import `PulseWeaver-Lumia-1.2.1.lumiaplugin` over the existing plugin after installing the Pulse Weaver build that provides native motion.

The motion catalogue includes named stage looks such as Game, Chatting, Printer and BRB. Run Stage Look / Motion Action triggers each look from Lumia reactions or LumiCon buttons. Restore Original Scenes returns the protected source state and original on-air scene even after repeated look changes or an app restart. The existing Stop + Restore and Restore Last controls remain available.

## Operating controls

- Start/end the whole configured show, or start/stop Twitch, Kick and YouTube individually.
- Activate an existing Stage, next Stage or previous Stage.
- Run a named stage look or motion action, stop and restore it, restore the last completed layout, or restore the original scenes.
- Show/hide/toggle an existing scene item.
- Mute/unmute/toggle and set volume (0–100%) on existing audio sources.
- Play, pause, restart, stop, next and previous on existing controllable media sources.
- Start/stop the recording mode already selected in Pulse Weaver.
- Reconnect after opening Pulse Weaver or changing its connection settings.

Platform start uses the saved horizontal/vertical/dual mode. A platform set to Off must be configured in Pulse Weaver first. Source mute and volume affect that source everywhere it is used; they do not edit platform audio exclusions. Visibility selects a specific scene/group item. Generated private routing copies are not exposed as editable scene items.

There are **no** scene/source creation or deletion actions, transforms, filters, file/URL replacement, configuration editing, or raw request actions. Lumia-labelled requests to the general editing API are rejected. The connection token is still the local API token, not a separately sandboxed credential; do not distribute it.

## Alerts and feedback

All 36 alerts default to **Off** in Lumia. Enable individual alerts when you configure their reactions. Existing saved alert switches are preserved by Lumia during an update; already-enabled alerts must be switched off once in Lumia. No additional plugin master switch is required.

36 alert definitions cover Stage changes, source shown/hidden/muted/unmuted/volume, media playback, transition begin/end, and platform/recording starting/live/stopping/stopped/failed (plus platform reconnecting). Output alerts include the specific output: a dual YouTube or recording route can produce separate output events. A platform's status confirms both YouTube outputs for Dual; partial failure is reported as failure rather than full success.

Existing status variables remain, with motion, Twitch, Kick and YouTube status variables. Action return variables keep Lumia's existing `pulseweavercontrol_` prefix. A start command only reports success after live state is observed; a timeout stops the Lumia action chain and tells you to check the app. A failed whole-show start may leave successful destinations running, and its error says so.

Events arrive over one authenticated local event stream. There is no idle status polling. Source lists update when their catalogue changes, variables/options update only when changed, and the connection sends a small heartbeat every 20 seconds while subscribed. Reconnection retries back off and stop after eight failed attempts; use Reconnect or re-enable the plugin afterwards. Reconnecting refreshes the current state without replaying historical alerts. Normal successful actions do not create toast spam or Event List entries.

## Connection

Port 18755 remains the default for the normal Pulse Weaver installation. The separate **Pulse Weaver Motion Preview** package uses manifest ID `pulseweavermotionpreview` and port 18765, so it can coexist with the release plugin. Build it with `packaging/Build-LumiaMotionPreview.ps1`. Its variables and action results use the `pulseweavermotionpreview_` prefix. Token discovery stays within the selected installation; it does not fall back to the other profile. For a custom portable installation, set the full `config/obs-studio/plugin_config/pulse-weaver-core/pulse-weaver.ini` path or enter its connection token. The task-specific package includes its configuration path, never its token.

Saved look requests are asynchronous: acceptance means the cue was queued, not that the animation has finished. A Stage change uses the configured transition before source movement. Within the same Stage, looks animate directly. Keep your Chatty layout actions in the same Lumia reaction; Pulse Weaver preserves Chatty as a locked full-canvas overlay.

Do not use Lumia's generic OBS connection to bypass these operating controls. Build scenes, Stages and routing inside Pulse Weaver.

## Verification

The Node test suite exercises pushed events, dynamic lists, platform isolation, completion/failure handling and rejection of unsupported actions. The native smoke test runs against an isolated development runtime, operates existing test sources, verifies their events and checks forbidden routes. No live platform broadcasts are started by these tests.
