# Create your show

Open **Show → Control → Build my show…**. The rebuilt wizard uses five phases: **Start, Sections, Looks, Sources and Review**. Choices remain drafts until **Create my show**.

A **Stage** is a section of your show, such as Starting or Game. A **Look** is an arrangement you switch to inside that Stage. A **source** supplies a camera, capture, image, video or browser overlay. Each Look has coordinated landscape and portrait versions. The diagrams are illustrations of placement; they do not open or test live sources.

## Build the show

1. **Start with your kind of show.** Choose **Full show**, **Game and content** or **Conversation**. These select a starting set of sections; they do not lock you into a template. Full show starts with five sections. Game and content starts with Game; Conversation starts with Chat. A feed-only show can be planned without Lumia. **Lumia setup…** explains when it is needed and links to installation, the compatible companion plugin, artwork and commands.
2. **Choose the sections you use.** Name the new show, then include Starting, Chat, Game, Projects, Celebration and Intermission as needed. The suggested name avoids existing shows. Ending and BRB belong to Intermission. Changing the selection does not erase the inactive draft choices.
3. **Shape your Looks.** Use the section tabs or named previous/next section actions. **Preview** opens a focused placement diagram without selecting the Look; **Include in my show** selects it. **Configure…** opens a focused editor with the placement preview alongside composition, crop/fit, inset position and portrait proportion. Compatible saved layouts retain framing and camera treatments. Each selected Stage needs at least one Look. Optional stage transitions have **Landscape · 16:9** and **Portrait · 9:16** tabs, each retaining its own default/saved behavior, Fade, Cut or stinger choice. **Add video…** collects a local video and cut point; it creates nothing yet.
4. **Give your show its sources.** Each question names the job of the feed and its stable A/B/C slot. Use **Use existing…**, **Create new…** or **Enter browser link…** where offered. New sources can be reused elsewhere through Use existing; creating another camera is unnecessary when an existing source already uses the device. **Fix next issue** jumps to missing or conflicting assignments. Use **Skip optional** for an unused optional feed. The count measures assignments and structural issues, not tested connections.
5. **Review your show.** Inspect the Stage cards and, when enabled, Shared layers. Each opens a focused review of the actual assignments; **Edit assignment** returns directly to that source. **Edit sections**, **Edit Looks** and **Edit sources** remain available. After editing sections or Looks, the **Review** shortcut returns to the final page, stopping at any step that still needs an answer. From Sources, choose **Review show**. With streaming, recording and other outputs stopped, choose **Create my show**.

Back preserves choices in the current wizard session. Cancel discards the wizard's source/stinger drafts. Closing and reopening starts a new draft; this is not a saved/resumable setup session.

## Reuse, artwork and optional layers

Feed A, B and C belong to their Stage. They can use the same source as another Stage or a different one. Reusing a source shares its content and later property changes; placement can differ between Looks.

Artwork questions are separate for each Look and canvas. Import and customize matching landscape (H) and portrait (V) templates in Lumia, then use its chain/copy-link button. Enter the **browser link**, not a template import link. Existing compatible browser sources can be reused. New artwork browser drafts are offered only for their matching orientation. Full-canvas placement is the default; choose a layout panel for artwork designed to sit beside a feed.

Entering the same link independently into separate questions makes separate browser definitions. To share one browser instance within the supported canvas, create a Browser link source once and select that same draft through Use existing elsewhere. Independent widget state and different orientations may need separate instances; the wizard does not silently merge them.

**Add shared layers** is off initially. Enable it on Sources to reveal three optional layers per canvas. Layer 1 is lowest and layer 3 is highest. Each can target all selected Looks, one Stage or one Look, including copied saved layouts. Empty layers create nothing. Turning the feature off preserves its in-session answers while excluding those layers from the new show. Inactive layers are not included in progress or final creation.

For a copied saved layout, required retained inputs still need compatible assignments. Hidden retained artwork is part of that graph; selecting fewer saved Looks does not automatically prune it.

## After creation

The completion screen explains the remaining checks. Creating a show does not start a broadcast or recording, test live delivery, or certify game-load performance.

**New browser sources start with output audio off; existing source audio settings are preserved.** Choose the intended audible copy and monitoring route, then check headphones and stream audio through a full Stage transition. Avoid enabling duplicate overlay audio merely because both canvases have a browser source.

Keep outputs stopped while checking the actual feeds, artwork, microphone/desktop audio, native movement and stingers on both canvases. A stinger runs when switching Stages; it does not replace movement between Looks inside a Stage. Keep its video in place and adjust timing in **Camera → Scene Transitions**. If hidden, show it through **Studio → Docks → Scene Transitions**. The wizard links to [Profounder](https://profounder.com/transitions/) for designs.

Finish Lumia's countdown, scene-switch and alert commands using the newly created Stages/Looks. The Lumia bulb on Show Control opens the guide again. Review destinations and encoders separately, then run a private rehearsal when ready.

## Lumia overlays and actions

The Lumia bulb beside the readiness status on **Show Control** opens the same guide available in the wizard. It contains all twelve import links, countdown start and completion instructions, Chat Physics setup, Custom Game Challenges profiles and native actions, and stinger guidance. Other supporting services can accompany Lumia in this area later.

Import only the required MAIN, READY, BRB, END, ALERT and NEWS families. H means landscape and V means portrait. Customize text, logos, colours and artwork in Lumia. An import link is different from a browser source link: after importing, use Lumia's chain / copy-link button and assign that link to the correct canvas.

Newsroom is visible on load by default. For an event-only news intro, import a separate copy and turn off **Show persistent Newsroom on load**. Give each Look the intended overlay copy.

Assign MAIN to Countdown, READY to Getting ready, BRB to Be right back and END to Ending. Add NEWS through the shared overlay questions and choose its intended Look. Match H/V artwork to the canvas. If you later deselect an overlay's target, choose another target or skip that overlay before creating the show.

For a fresh **Custom Game Challenges** setup, skip ALERT01-H's old seven-command bundle and use the optional plugin's native actions. The public ALERT templates retain a legacy `FN_Challenge` widget; disable that challenge layer when adding the new display, keeping unrelated alerts. The portrait template has no command bundle. Neither public template automatically connects to the new plugin. Existing connected users may keep their controls and skip example imports. See [Custom Game Challenges 1.0.5 setup](<LUMIA-GAME-CHALLENGES-2026-10-14.md>) for the display, correct import destinations and manual Twitch reward linking. New browser sources start with output audio excluded; existing browser audio settings are preserved. Enable audio for one intended source in audio routing after creation and keep its other canvas copy silent, so alerts, TTS and effects do not play twice.

The shared Lumia countdown starts through the `start-screen` payload described in the guide: `{"action":"start","durationSeconds":600}` starts ten minutes. A Pulse Weaver **Run Stage Look / Motion Action** selects the Starting Countdown Look; the separate payload starts the timer. Send the payload once for both formats.

To switch when the timer reaches zero, create your own Lumia command, for example `countdown-finished`, containing Pulse Weaver's **Run Stage Look / Motion Action** with the next Stage / Look selected. In one Starting overlay's Lumia settings, set **Your countdown completion command** (`goLiveCommand`) to that command name and enable **Run completion command (enable on ONE canvas only)** (`autoFireGoLive`). Leave this off in the other orientation and any other timer copies. The shared templates start with completion disabled and the command blank. Choose one command owner across all timer instances, separately from the one audible owner for overlay sound. Starting a broadcast is a separate action; only add it to your command if that is intended.

With outputs stopped, select the Starting Look, send a short countdown, and confirm it calls your command exactly once and reaches the intended next Look. The Lumia bulb on Show Control keeps these start, stop and completion instructions available after the wizard closes.

Native Credits use Lumia's session data. Repeated activation uses the tested viewer compatibility bridge; populated session entries and separate-account import still need acceptance testing.

## Chat Box Physics

Open [Chat Box Physics in the Lumia plugin marketplace](https://lumiastream.com/plugins#chat_physics_local), confirm the creator is **TangoTiddies**, and choose **Install Plugin on Lumia Stream**. Enable it in Lumia. Its installed settings and actions may use the internal name **Chat Physics Local**. Enable **Lumia Settings → API → Developers API** and put its token only in the plugin's **Connection → Lumia Developers API token** setting. Use Lumia's displayed API port, normally `39231`, with host `127.0.0.1`.

On the Sources step, enable shared layers. Chat Physics is suggested in **shared overlay layer 2** for both formats. Click **Use default Chat Box Physics link** to fill the matching URL: `http://127.0.0.1:39270/` for landscape and `http://127.0.0.1:39270/portrait` for portrait. You can choose another free layer. Enter the plugin's **Local overlay port** if changed. These are separate browser sources, with source viewports **1920 × 1080** and **1080 × 1920** respectively. Fit each to its destination canvas. Choose which Looks should show it and keep source shutdown and scene-activation refresh off so cards survive Look changes. One plugin connection feeds both formats.

To coordinate chat with a camera layout, put **Change Chat Camera Clearances**, **Change Chat Physics Mode** and Pulse Weaver's **Run Stage Look / Motion Action** in the same Lumia command. Set each action's **Output** to Landscape, Portrait or Both as appropriate. Clearances reserve source pixels above, below and beside upright chat; choose **Upright chat box** or **Compact single line** and the desired placement. Different camera layouts usually need separate H/V settings. Clearances do not make chat automatically follow moving cameras, and physics modes use their own placement areas.

Set **Revert after seconds** to **0** to keep the selected chat layout. A positive value is a hold time before reverting, not the animation duration. Existing cards keep their mode; use **Clear existing cards first** when the whole presentation should change. **Restore Saved Chat Layout** returns the selected output to its saved layout. Use **Preview Chat Physics**, then a connected chat message, to check both outputs with streaming and recording stopped. The **Chat Physics** topic in the Lumia bulb guide provides the full steps and troubleshooting.

## Choose your chat panel

Named web chat panels are available in Pulse Weaver 1.14.2 and later.

On **Show Control → Show**, click the heading text to cycle through native **Chat** and your saved panels, shown by the names you choose. Click the adjacent emblem and choose **Add chat…** to enter a **Name** and complete HTTP or HTTPS **Link**. You can add any compatible web chat page. Each panel keeps its own name, link and page size; editing one panel does not replace another. The page appears inside the main Chat card, which retains its normal 280 px minimum width.

With a saved panel selected, its emblem menu offers **Edit this chat…**, **Reload**, **Open in browser**, **Page size** and **Remove this chat panel**. Returning to **Chat** restores Pulse Weaver's usual feed and controls; its emblem offers the generic **Add chat…** action.

For example, name a panel **Lumia** and use `https://lumiastream.com/dock/chat` for its focused Chatbox. A new Lumia panel starts at **67%**; other new panels start at 100%. Saved sizes are retained independently. **Lumia view** options appear only when the selected panel's link uses the exact `lumiastream.com` or `www.lumiastream.com` host. They switch that panel between **Show Lumia chat only** and **Show full Control Room**. The native Chat menu provides the same generic add flow for every service.

External pages use the current app's shared browser context and retained login storage. Visited pages remain loaded when you return to **Chat**, select another panel or leave the Show view, preserving drafts and scroll position. **Reload** refreshes only the selected panel. Removing a panel or closing Pulse Weaver closes its page. Sign in to a third-party service yourself when it asks. Pulse Weaver's connected chat events remain available to Action rules. Changing the displayed panel does not promise reduced YouTube receiving quota: the current incoming YouTube reader already uses web chat.

The [previous theme-builder guide](<BUILD-MY-SHOW-GUIDE.md>) is historical; its page sequence and default Look catalogue do not describe the current wizard.
