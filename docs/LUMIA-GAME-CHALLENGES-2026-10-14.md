# Custom Game Challenges 1.0.5 — Lumia setup update

14 October 2026. This updates the common guide opened by the wizard and Show Control's Lumia bulb. It changes instructions only. Existing rewards, prices, enabled states, button bindings, shows and published overlay packages are preserved.

## Install and display

Import the supplied `game-challenges-1.0.5.lumiaplugin` through Lumia's Plugins screen, enable **Custom Game Challenges** (`game_challenges`), then choose **Open Challenge Manager** in its settings. This is a separate optional plugin; keep the Pulse Weaver companion plugin (`pulseweavercontrol`), its connection settings and Chat Box Physics. Version 1.0.5 is a prepared upload package; no public marketplace listing/download has been verified, so this guide provides no invented download link.

The default Fortnite profile contains **20 editable challenges**. Create, duplicate or import other game libraries in **Games & challenges**; they are not supplied automatically. The other manager tabs are **Current & queue**, **Display & branding**, **Setup & help**, and **Import & backup**.

1. In **Setup & help**, select **Copy layer HTML**.
2. In a Lumia overlay, add a full-canvas **Custom layer**. Paste into **HTML**, leaving CSS and JavaScript empty. Repeat in the required H/V overlay documents, with the same controller. For a regular browser source, use **Copy display URL** directly.
3. Add the resulting browser links to the wizard's appropriate shared alert/overlay slots, with independent full-canvas landscape and portrait assignments. A template import/share URL is not a browser link. No additional Stages or operating Looks are needed for the widget or its colour variants.
4. Configure native actions, test privately with outputs stopped, link your own Twitch rewards, then enable only the controls you want.

The display runs on the Lumia/controller PC; remote-PC hosting is not provided in this release. Normal setup uses the manager's copy buttons, without entering controller keys or hardcoded ports.

## Native actions and saved profiles

Use **Roll a challenge**, **Choose a challenge**, or **Viewer / custom challenge**, choosing a specific profile or **Currently active game**. Rolls can filter Any, Easy, Medium, Hard or Extreme, with optional tags. Specific challenges come from the selected profile's dropdown. Use Lumia's variable picker for viewer text/requester fields on other trigger types.

To make reusable tier rewards, choose **Roll a challenge → Currently active game → chosen difficulty**. Keep **Add to this game's queue** unless replacement is intended. Enable **Announce once in chat** only if wanted; native actions and examples have chat off. One controller selects, saves and optionally announces each result. H/V displays render the same snapshot, without separate rolls or chat actions.

Each saved profile retains its library, current challenge, waiting queue, history and appearance through profile switches and Lumia restarts. Use the manager or **Switch game profile**; there is no automatic game detection. Switching does not roll, clear or announce anything. Timers pause in inactive profiles, when paused and while Lumia is closed. Visual auto-hide does not advance the queue.

**Next challenge** dismisses exactly one current challenge and shows the next waiting entry. Its outcome can be Skipped, Completed or Failed. **Clear challenges** is separate: its default **Waiting queue only — keep current** preserves the current challenge; **Current and waiting queue** clears both for that profile. Clear requires the configuration confirmation checkbox. **Undo last dismissal / clear** restores the last supported change. No reset-all example is bundled.

In **Display & branding**, edit style, colours, font, uploaded logo, width/scale, position and animation. Appearance edits do not roll or clear anything. A display style is not a saved game profile or Pulse Weaver Look. Profile packs share libraries and branding; full backups also contain saved queues/history and viewer text and should be kept private.

## Optional examples

Accept **Install bundled commands** during plugin installation, or skip it and create your own actions. All six files start **disabled and unlinked**, with no private keys, Twitch reward IDs or hardware bindings. Lumia's per-file destination prompt initially selects **Chat Commands**, even for a reward.

| Imported name | Destination | Suggested points | Action |
| --- | --- | ---: | --- |
| GC Example - Fortnite Challenge | Twitch Points Commands | 1,500 | Roll Fortnite, Any difficulty |
| GC Example - Hip-Fire Only | Twitch Points Commands | 2,000 | Choose `fn-8` |
| GC Example - Custom Challenge | Twitch Points Commands | 5,000 | Viewer text; input required |
| GC Example - First Gun Only | Twitch Points Commands | 2,000 | Choose `fn-18` |
| GC Example - No High Ground | Twitch Points Commands | 2,000 | Choose `fn-16` |
| gc-next | Chat Commands | — | Next, active game, Skipped outcome |

For every **GC Example** choose **Twitch Points Commands** or **Use file target (Twitch Points Commands)**. Import `gc-next` into **Chat Commands**; with the default prefix it is `!gc-next`, restricted to Streamer and Mod by default. Review permissions if Lumia's user levels were customized.

Under **Commands → Twitch Points**, review prices and manually create/link rewards to your own Twitch account. Custom Challenge requires viewer input and uses `{{message}}` for text and `{{displayname}}` for the requester. Test privately, then enable only desired controls. The five rewards stay pinned to Fortnite; Next follows the active game. Specific challenge IDs must change when using another profile's library.

To import later, extract the six `.lumia` files from `bundle/commands/` in the `.lumiaplugin` ZIP and use Lumia's command import controls with the destinations above. Skip duplicate copies if already imported. A hardware button can use native **Next challenge** without a bundled device binding.

## Public templates and existing setups

**PW-ALERT01-H still includes the old seven-command bundle:** five disabled `pwfn...` Code helpers plus `clearchallenge` and `resetchallenge`. PW-ALERT01-V has no command bundle. The archived H/V definitions retain the legacy `FN_Challenge` widget. Guide changes do not change the published templates or connect them to the new controller.

For fresh plugin setups, skip that old command bundle. Disable the legacy challenge widget where present when adding the new display, retaining unrelated alerts. Avoid showing both widgets or installing two control systems for one queue. The public import path has not been certified against a fresh account; a private compatibility bridge is not evidence of automatic public integration.

Existing users may keep their working compatibility adapters and skip example imports. Preserve reward names, prices, enabled states and unrelated actions. Existing connected rewards and button 34 remain pinned to Fortnite; button 34 keeps its skip-current-and-advance meaning. Deliberate migration must choose native Next or an explicit Clear scope based on intended behavior, without guessing from an old control's name or blindly forwarding old payloads.

Original artwork is preserved by connected private bridges; new plugin branding applies to new plugin displays. Private controller values/placeholders are not a public paste-and-go setup.

## Source and validation record

The selected checkout is `worktrees/alpha2-installer-release`, branch `codex/alpha2-installer-config-fix`, baseline `879c6b6` (1.14.4-alpha.2, 13 October). Its wizard and frontend both call `PulseInstructions::openDialog`; the common source is `engine/obs-studio/shared/qt/PulseInstructions.hpp`. Older copies in unstable/Lite are not patched by this update.

Before pushing the requested alpha-branch update, GitHub was refreshed and the newer `v1.14.4-alpha.4` base (`d2bee94`) was identified. The guide update is carried forward onto that base for `codex/alpha`, retaining the alpha 2 installer repair and alpha 3/4 Altron additions. The affected guide and wizard files are identical between those bases before this update, so the recorded build/UI checks still cover the changed production sources.

Read against the complete `PULSEWEAVER-GUIDE-HANDOFF.md` and the plugin's current SETUP, ACTIONS, BUNDLED-EXAMPLES, manifest, DEVELOPER and marketplace listing files. The 10 October asset manifest and local alert exports confirm the retained legacy template boundary; this is not a new-account import/linking test. Earlier wizard records remain historical evidence, not tests of this plugin.

Validation on 14 October:

- MSVC builds passed for `tests/LumiaGuide` (the actual shared guide header) and `tests/MotionAudit` (including the production wizard, motion engine and show builder from this checkout). The latter linked a compatible existing OBS runtime; its CMake source root is this checkout, not the historical runtime's source tree. This is not a full distributable frontend/installer build.
- The isolated native motion/wizard audit passed **1,954 checks**, using inert sources and temporary fixtures. The sandbox's default temporary directory initially prevented a fixture write; rerunning with the fixture directory inside this workspace passed.
- The guide UI harness passed **61 checks at 100% and 61 at 150% scaling**. It exercised the common modal helper under both caller types, identical content, all five topics, 900×700 / 760×560 / 640×480 windows, keyboard scrolling, the bottom migration note, Close, and an intercepted click on the original Lumia download link. Compact and high-DPI challenge screenshots were visually inspected.
- Source review confirms both production callers still use `PulseInstructions::openDialog`. Original external-link destinations and the complete Starting countdown, Chat Physics and Stingers topic contents were retained. Newsroom, native Credits and companion connection text outside the focused overlay/challenge edits is unchanged.

Logs and UI captures are in the workspace's `artifacts/game-challenges-guide-2026-10-14/`. No release publication, plugin installation, live redeems/chat, broadcast control, application shutdown or user-show mutation was performed. Fresh-account template migration and Twitch reward linking remain unverified; plugin package tests supplied in the handoff are not presented as new Pulse Weaver acceptance tests.
