# Pulse Weaver for Lumia Stream — 1.1.3

Requires **Pulse Weaver 1.11.42 or newer** and Lumia Stream 9.x. Install the companion Pulse Weaver update first, then import `PulseWeaver-Lumia-1.1.3.lumiaplugin` into Lumia and enable it. The plugin includes the native neon P icon and keeps its existing `pulseweavercontrol` ID so Lumia can update the existing plugin.

Version 1.1.3 reads the current output plan immediately before a Lumia Start Show or Start Platform action, so a recently enabled Kick, Twitch or YouTube route cannot be skipped by an older cached snapshot. It retains the 1.1.2 End Stream fix: repeated stops never request a start, and a newer show command cancels pending cleanup. There is no new background polling while idle.

## Operating controls

- Start/end the whole configured show, or start/stop Twitch, Kick and YouTube individually.
- Activate an existing Stage, next Stage or previous Stage.
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

Existing status variables remain, with separate Twitch/Kick/YouTube status variables added. Action return variables use Lumia's required `pulseweavercontrol_` prefix. A start command only reports success after live state is observed; a timeout stops the Lumia action chain and tells you to check the app. A failed whole-show start may leave successful destinations running, and its error says so.

Events arrive over one authenticated local event stream. There is no idle status polling. Source lists update when their catalogue changes, variables/options update only when changed, and the connection sends a small heartbeat every 20 seconds while subscribed. Reconnection retries back off and stop after eight failed attempts; use Reconnect or re-enable the plugin afterwards. Reconnecting refreshes the current state without replaying historical alerts. Normal successful actions do not create toast spam or Event List entries.

## Connection

The plugin automatically reads the installed Pulse Weaver configuration beneath `%LOCALAPPDATA%/Programs/Pulse Weaver`. For a custom portable installation, set its full `config/obs-studio/plugin_config/pulse-weaver-core/pulse-weaver.ini` path, or enter its connection token. Loopback port defaults to 18755.

Do not use Lumia's generic OBS connection to bypass these operating controls. Build scenes, Stages and routing inside Pulse Weaver.

## Verification

The Node test suite exercises pushed events, dynamic lists, platform isolation, completion/failure handling and rejection of unsupported actions. The native smoke test runs against an isolated development runtime, operates existing test sources, verifies their events and checks forbidden routes. No live platform broadcasts are started by these tests.
