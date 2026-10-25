# Create your show

Open **Show → Control → Build my show…**. The main route starts with what you stream and the pictures you actually have. It does not require Lumia or knowledge of Stages, Looks or Feed A/B/C.

## Six focused steps

1. **Activity — what do you stream?** Choose Games, Chat and shared content, Making/music/teaching, or Cameras and guests. Choosing a card continues to Inputs. These describe activities; all can include optional Starting, Break and Ending screens. **Use saved layouts / detailed setup…** opens the retained detailed builder for existing Stage layouts and specialist configuration.
2. **Inputs — what will viewers see?** Choose your game/content, optional camera, and optional second angle or guest. Reuse an existing input or configure one new device, window, image, video or browser link. Bind each input once: views share its identity and properties. A second angle needs a different input. Games and making/teaching need a content/work input; a camera is optional. Guest setup captures a camera or existing call window/browser feed; it does not host or connect a new call service.
3. **Views — which pictures will you switch between?** Only views supported by those inputs are offered. Content-only, camera-only, combined content/camera, detail/guest, two-input comparison and three-input views have different visible subjects or operating purposes. A different camera size is a framing choice, not another preset. **Preview this view** leaves inclusion unchanged. **Framing…** controls explicit camera/detail cropping; game and shared content retain their whole picture. **Screen transitions…** keeps separate landscape/portrait Fade, Cut and existing/new stingers.
4. **Screens — what surrounds Main?** Name the show and include Starting, Break and Ending as needed. Built-in, editable-message image cards work immediately without an external browser or timer. Starting is a static holding message, not a running countdown. **Use my own browser artwork…** lets you replace either format independently; missing formats keep their built-in fallback. Use a real provider browser link for a live countdown. Turning a screen off preserves its in-session answers and omits it from creation.
5. **Alerts — up to three layers.** Each numbered card contains landscape and portrait settings together. Add a provider browser link or reuse an existing overlay. One responsive link can create matching H/V browser viewports; separate links are available. Choose whether the layer appears on every screen or only Main. Layer 1 is below 2 and 3; all sit above the pictures. Empty/removed layers create no browser instances. This is optional: continue without alerts if they are not ready.
6. **Review — inspect what will be created.** Browse the actual selected view compositions, source names, screens and per-format alert/artwork assignments. Change a specific section, then use **Review** to return, stopping at any incomplete answer. With all outputs stopped, choose **Create my show**. The new Main section is selected in Control for inspection. Existing shows and source settings are retained.

Back preserves the current session's answers. Preview is an illustration and does not open devices. Cancel adds no partial show or source/stinger drafts. Closing and reopening starts a new draft; resumable sessions are not implemented.

## Understand the result

In the main application, a show screen such as Main, Starting or Break is called a **Stage**. The views you switch between inside Main are called **Looks**. Inputs are **sources**. These terms describe the result; they are not prerequisites for the first setup decision.

Camera/content inputs are reused across the selected views and both canvases. Newly drafted browser inputs have a viewport for each used canvas, rather than a separate instance for every Look. Static screen/background PNGs use ordinary image sources. The wizard adds no autoplay, live demonstration decoder or idle polling timer.

Wide game, slide or call pictures cannot fill a tall frame without space around them or cropping. The preview shows fitted picture extent using known input dimensions; unknown uncreated captures use a labelled 16:9 example. New browser viewports use their respective canvas shape. Camera/detail cropping is opt-in. Check important game HUD, text and people on the actual landscape and portrait outputs; a schematic does not certify their readability.

## Alerts, artwork and sound

Lumia, StreamElements, Streamlabs and other compatible HTTP/HTTPS browser overlays can be assigned. A Lumia template import link must first be imported and customized in Lumia; then use its chain/copy browser link. The app's internal Overlay Designer preview links are not provider alert links.

Each alert layer is one logical slot with independent H/V assignments and visibility. A landscape-only choice remains landscape-only when reopened. Reusing a source shares its content and later properties; unrelated links or devices are not merged automatically. A new responsive link uses matching browser viewport dimensions for the selected formats. Existing wide/tall overlays are filtered to their matching format.

**Existing audio and monitoring settings are retained. Every newly drafted browser copy starts with output audio off, including browser content/guest feeds.** Choose one intended audible copy and its monitoring route in the real mixer after creation, then listen to both headphones and stream/recorded audio through a full transition. Do not enable both copies merely because two canvases exist. Picture assignment does not test provider delivery, a guest call or audio playback.

Screen changes do not claim to mute the microphone. Starting/Break/Ending are visual operating states; review audio routes separately. Stingers run between Stages, while native movement runs between Looks. Keep stinger video files in place and adjust their cut point in **Camera → Scene Transitions**; show that dock through **Studio → Docks** if needed.

The post-creation screen explains picture/movement, sound, Lumia commands and destinations. Check the real feeds and fonts on both outputs, verify audio once, then rehearse privately with the intended game and encoders. Creation itself never starts streaming or recording and does not prove game-load performance.

## Detailed setup and saved layouts

The opening's **Use saved layouts / detailed setup…** preserves the previous Stage-specific builder, saved framing/treatments, per-stage inputs and transitions, independent artwork placement and scoped shared overlays. Its source and graph constraints remain in place. Use it for copied layouts or specialist per-stage assignments; existing shows are not migrated or pruned by the task-first wizard.

Detailed setup still has three shared layers per canvas. A copied saved layout can retain hidden inputs, so required graph-compatible assignments may remain necessary. Skipping an input hides it; it does not necessarily remove its retained definition.

## Lumia overlays and actions

The Lumia bulb beside the readiness status on **Show Control** opens the same guide available in the wizard. It contains all twelve import links, countdown start and completion instructions, Chat Physics setup, Custom Game Challenges profiles and native actions, and stinger guidance. Other supporting services can accompany Lumia in this area later.

Import only the required MAIN, READY, BRB, END, ALERT and NEWS families. H means landscape and V means portrait. Customize text, logos, colours and artwork in Lumia. An import link is different from a browser source link: after importing, use Lumia's chain / copy-link button and assign that link to the correct canvas.

Newsroom is visible on load by default. For an event-only news intro, import a separate copy and turn off **Show persistent Newsroom on load**. Give each Look the intended overlay copy.

In detailed setup, assign MAIN to Countdown, READY to Getting ready, BRB to Be right back and END to Ending. In the task-first route, assign the appropriate matching browser artwork through Starting, Break or Ending's customization screen. A live MAIN countdown still needs its Lumia commands; the built-in Starting image has no timer. Use an Alerts slot for shared NEWS artwork, or detailed setup for specific-Look targeting. Match H/V artwork to the canvas. If you later deselect a detailed overlay's target, choose another target or skip that overlay before creating the show.

For a fresh **Custom Game Challenges** setup, skip ALERT01-H's old seven-command bundle and use the optional plugin's native actions. The public ALERT templates retain a legacy `FN_Challenge` widget; disable that challenge layer when adding the new display, keeping unrelated alerts. The portrait template has no command bundle. Neither public template automatically connects to the new plugin. Existing connected users may keep their controls and skip example imports. See [Custom Game Challenges 1.0.5 setup](<LUMIA-GAME-CHALLENGES-2026-10-14.md>) for the display, correct import destinations and manual Twitch reward linking. New browser sources start with output audio excluded; existing browser audio settings are preserved. Enable audio for one intended source in audio routing after creation and keep its other canvas copy silent, so alerts, TTS and effects do not play twice.

The shared Lumia countdown starts through the `start-screen` payload described in the guide: `{"action":"start","durationSeconds":600}` starts ten minutes. A Pulse Weaver **Run Stage Look / Motion Action** selects the Starting Countdown Look; the separate payload starts the timer. Send the payload once for both formats.

To switch when the timer reaches zero, create your own Lumia command, for example `countdown-finished`, containing Pulse Weaver's **Run Stage Look / Motion Action** with the next Stage / Look selected. In one Starting overlay's Lumia settings, set **Your countdown completion command** (`goLiveCommand`) to that command name and enable **Run completion command (enable on ONE canvas only)** (`autoFireGoLive`). Leave this off in the other orientation and any other timer copies. The shared templates start with completion disabled and the command blank. Choose one command owner across all timer instances, separately from the one audible owner for overlay sound. Starting a broadcast is a separate action; only add it to your command if that is intended.

With outputs stopped, select the Starting Look, send a short countdown, and confirm it calls your command exactly once and reaches the intended next Look. The Lumia bulb on Show Control keeps these start, stop and completion instructions available after the wizard closes.

Native Credits use Lumia's session data. Repeated activation uses the tested viewer compatibility bridge; populated session entries and separate-account import still need acceptance testing.

## Chat Box Physics

Open [Chat Box Physics in the Lumia plugin marketplace](https://lumiastream.com/plugins#chat_physics_local), confirm the creator is **TangoTiddies**, and choose **Install Plugin on Lumia Stream**. Enable it in Lumia. Its installed settings and actions may use the internal name **Chat Physics Local**. Enable **Lumia Settings → API → Developers API** and put its token only in the plugin's **Connection → Lumia Developers API token** setting. Use Lumia's displayed API port, normally `39231`, with host `127.0.0.1`.

In task-first setup, use a free **Alerts and overlays** layer and enter separate format links: `http://127.0.0.1:39270/` for landscape and `http://127.0.0.1:39270/portrait` for portrait. In detailed setup, enable shared layers on Sources; Chat Physics is suggested in layer 2, and **Use default Chat Box Physics link** fills those matching URLs. Enter the plugin's **Local overlay port** if changed. These are separate browser sources, with source viewports **1920 × 1080** and **1080 × 1920** respectively. Fit each to its destination canvas. Use detailed setup for individual-Look targeting. Keep source shutdown and scene-activation refresh off so cards survive Look changes. One plugin connection feeds both formats.

To coordinate chat with a camera layout, put **Change Chat Camera Clearances**, **Change Chat Physics Mode** and Pulse Weaver's **Run Stage Look / Motion Action** in the same Lumia command. Set each action's **Output** to Landscape, Portrait or Both as appropriate. Clearances reserve source pixels above, below and beside upright chat; choose **Upright chat box** or **Compact single line** and the desired placement. Different camera layouts usually need separate H/V settings. Clearances do not make chat automatically follow moving cameras, and physics modes use their own placement areas.

Set **Revert after seconds** to **0** to keep the selected chat layout. A positive value is a hold time before reverting, not the animation duration. Existing cards keep their mode; use **Clear existing cards first** when the whole presentation should change. **Restore Saved Chat Layout** returns the selected output to its saved layout. Use **Preview Chat Physics**, then a connected chat message, to check both outputs with streaming and recording stopped. The **Chat Physics** topic in the Lumia bulb guide provides the full steps and troubleshooting.

## Choose your chat panel

Named web chat panels are available in Pulse Weaver 1.14.2 and later.

On **Show Control → Show**, click the heading text to cycle through native **Chat** and your saved panels, shown by the names you choose. Click the adjacent emblem and choose **Add chat…** to enter a **Name** and complete HTTP or HTTPS **Link**. You can add any compatible web chat page. Each panel keeps its own name, link and page size; editing one panel does not replace another. The page appears inside the main Chat card, which retains its normal 280 px minimum width.

With a saved panel selected, its emblem menu offers **Edit this chat…**, **Reload**, **Open in browser**, **Page size** and **Remove this chat panel**. Returning to **Chat** restores Pulse Weaver's usual feed and controls; its emblem offers the generic **Add chat…** action.

For example, name a panel **Lumia** and use `https://lumiastream.com/dock/chat` for its focused Chatbox. A new Lumia panel starts at **67%**; other new panels start at 100%. Saved sizes are retained independently. **Lumia view** options appear only when the selected panel's link uses the exact `lumiastream.com` or `www.lumiastream.com` host. They switch that panel between **Show Lumia chat only** and **Show full Control Room**. The native Chat menu provides the same generic add flow for every service.

External pages use the current app's shared browser context and retained login storage. Visited pages remain loaded when you return to **Chat**, select another panel or leave the Show view, preserving drafts and scroll position. **Reload** refreshes only the selected panel. Removing a panel or closing Pulse Weaver closes its page. Sign in to a third-party service yourself when it asks. Pulse Weaver's connected chat events remain available to Action rules. Changing the displayed panel does not promise reduced YouTube receiving quota: the current incoming YouTube reader already uses web chat.

The [previous theme-builder guide](<BUILD-MY-SHOW-GUIDE.md>) is historical; its page sequence and default Look catalogue do not describe the current wizard.
