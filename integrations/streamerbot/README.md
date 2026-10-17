# Pulse Weaver 2.0 preview: basic controls for Streamer.bot

This integration sends a small set of operating commands to Pulse Weaver's existing local HTTP API. It uses the configured Stage, scene items, audio sources and recording mode. No platform credentials or extra streaming encoders are needed.

## Set up an action

1. Open the 2.0 preview and find its Local API port and connection token. A fresh preview defaults to **18775**; a copied show keeps its explicitly saved port. Regular Pulse Weaver defaults to 18755 and the motion preview to 18765.
2. In Streamer.bot, set private persistent global variables `pulseweaver_host` to `127.0.0.1`, `pulseweaver_port` to that installation's port, and `pulseweaver_token` to its token. The token gives access to the existing local API; keep it out of exports, chat messages and screenshots. The example accepts loopback hosts only.
3. Create an action with a **Set Argument** sub-action named `pulseweaver_request`. Paste one `request` object from [commands.json](commands.json) as its string value. Replace names/IDs with values from your show.
4. Add **Execute C# Code** and paste [PulseWeaverBasicControls.cs](PulseWeaverBasicControls.cs). Compile it inside Streamer.bot; its normal `System.Net.Http` and Newtonsoft.Json references are required.
5. Attach a chosen button, hotkey or other Streamer.bot trigger to this action. Use curated command arguments; avoid passing arbitrary chat input directly into the command JSON.

The C# action returns false on a rejected request or connection error. Following sub-actions can inspect `pulseweaver_ok`, `pulseweaver_status`, `pulseweaver_message` and `pulseweaver_state` (JSON). Requests receive a fresh correlation ID; that ID is echoed, not an automatic retry/deduplication mechanism. The script does not print tokens or raw headers.

Streamer.bot documents C# `HttpClient` requests and handling their responses in its [HTTP example](https://docs.streamer.bot/examples/http-post). This integration calls **Pulse Weaver**. Streamer.bot's own [HTTP DoAction](https://docs.streamer.bot/api/http/requests/do-action) and [WebSocket DoAction](https://docs.streamer.bot/api/websocket/requests) run actions inside Streamer.bot; they are different endpoints and are not required here.

## Commands and identifiers

Send `GET /api/v1/control` with `Authorization: Bearer <token>` to discover supported commands, friendly Stage names, existing source UUIDs, scene/group item IDs, current state and availability. Send a JSON object to `POST /api/v1/control` with the same header. The optional `X-Pulse-Weaver-Client: streamerbot` label identifies this example; authorization still comes from the existing token.

| Command | Required fields | Behaviour |
| --- | --- | --- |
| `stage.select` | `name` | Select an existing Stage by its exact friendly name. |
| `stage.next`, `stage.previous` | None | Move through the configured list, wrapping at its ends. |
| `source.show`, `source.hide`, `source.toggle` | `sceneId`, `itemId` | Operate one existing item in a scene/group. `sceneId` is its parent's UUID; `itemId` is the catalogue's decimal **string**. |
| `audio.mute`, `audio.unmute`, `audio.toggle` | `sourceId` | Change the existing audio source everywhere it is used. |
| `audio.volume` | `sourceId`, `volume` | Set linear volume percent, a JSON number from 0 to 100. |
| `recording.start`, `recording.stop` | None | Request the currently selected native recording mode, including Default or routed formats. Off must be changed inside Pulse Weaver first. |
| `replay.save` | None | Save an already running replay buffer. It never starts a buffer implicitly. |

Each response contains `ok`, `accepted`, `command`, `code`, `message` and observed `state`. Applied Stage/source operations return HTTP 200. Stage transitions may still be running. Recording/replay requests return HTTP 202; acceptance does not confirm a successful start or completed file. Native recording validation and operator confirmations remain authoritative. Check Pulse Weaver's actual state and any error shown there before continuing a time-critical chain.

HTTP 400 indicates an unknown command or invalid argument; 404 a missing target; 409 an unavailable control, unsupported source, or native rejection. Numeric item IDs are rejected because large IDs lose precision in JSON numbers. UUID/item targeting avoids changing the wrong item when names are repeated. Volume/mute do not edit monitoring modes, tracks or copyright exclusions. Existing Lumia bindings and its restricted routes are unchanged.

The control vocabulary follows familiar source-mute and scene-item operations in the official [OBS WebSocket protocol](https://github.com/obsproject/obs-websocket/blob/master/docs/generated/protocol.md); it is **not** an implementation of that protocol. Replay requests use OBS's native [frontend API](https://docs.obsproject.com/reference-frontend-api). No Twitch, YouTube, Kick or other platform SDK client, inbound Streamer.bot event listener, arbitrary-code command, stream-start/end command, source creation/deletion or file/URL replacement is implemented by this basic surface.

## Verification

`tests/StreamerBotIntegration` exercises the production command helper and category/detail page with inert Qt controllers and source fixtures. It covers native vetoes, duplicate labels, large string item IDs, volume boundaries, disabled/Default recording modes, stale displayed recording status, inactive replay rejection, response correlation, masked credentials and browsing examples without executing commands. The fixture starts no broadcasts and writes no recording/replay files.

`tests/StreamerBotIntegration/adapter` compiles the unchanged C# example with a Streamer.bot API stand-in and checks its HTTP requests against an inert loopback server: authentication headers, command/correlation data, success/error/redirect feedback, remote-host rejection and safe logging. It does not verify the actual Streamer.bot application's compiler/import UI. `commands.json` is an examples catalogue, not a fabricated Streamer.bot import bundle.

The native 2.0 fixture also passed authenticated HTTP checks against a disposable portable configuration: Stage select/next/previous, visibility, mute/unmute, observed volume, invalid-command feedback, restricted Lumia routes, repeated stop-recording, and inactive-replay rejection. It used empty scene/media fixtures and started no broadcast, recording or replay outputs.
