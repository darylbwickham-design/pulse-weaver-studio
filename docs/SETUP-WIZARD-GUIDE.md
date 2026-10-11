# Create your show

Open **Show Control → Control → Build my show…**. The wizard has one flow with layout options available by default.

1. **Connect Lumia.** Install Lumia Stream, add the Pulse Weaver companion plugin and import the templates your chosen Looks use. **Get Pulse Weaver overlays** opens the import links and setup guidance. You can proceed with existing sources. These are manual readiness checks.
2. **Choose stage groups.** Select Starting, Chat, Game, Projects, Celebration and Intermission as needed. Ending and BRB belong to Intermission.
3. **Choose functional Looks and options.** Select only the operating views you need. Configure composition, fit/crop, inset position and portrait proportion within those Looks. Existing saved presets appear when a compatible local reference is available; they retain their framing and camera treatments. A fresh installation uses the general layout options.
4. **Assign sources and review.** Assign Feed A, B and C according to each question. Reuse cameras or captures where useful, skip optional feeds, and add your own overlay browser links for each canvas. Review both orientations and choose **Create my show** with all outputs stopped.

Related Looks share sources so Pulse Weaver Move can animate between them. Switching Stages uses a transition. Optional stingers are separate landscape and portrait choices; the Lumia guide links to Profounder. Existing shows and device settings remain available.

## Lumia overlays and actions

The Lumia bulb beside the readiness status on **Show Control** opens the same guide available in the wizard. It contains all twelve import links, countdown and Stage-switch payload instructions, Fortnite challenge setup and stinger guidance. Other supporting services can accompany Lumia in this area later.

Import only the required MAIN, READY, BRB, END, ALERT and NEWS families. H means landscape and V means portrait. Customize text, logos, colours and artwork in Lumia. An import link is different from a browser source link: after importing, use Lumia's chain / copy-link button and assign that link to the correct canvas.

Newsroom is visible on load by default. For an event-only news intro, import a separate copy and turn off **Show persistent Newsroom on load**. Give each Look the intended overlay copy.

Import ALERT01-H's accompanying commands once. Its two chat controls and five disabled reward-code helpers serve both orientations. Copy the helpers into your own Twitch Points rewards and choose costs and permissions. The portrait copy carries no duplicate commands. Use one audible owner across overlays so alerts, TTS and effects do not play twice.

The shared countdown starts through the `start-screen` payload described in the guide. Set its next Stage / Look command yourself and enable automatic completion in one instance only. Shared templates have automatic completion disabled by default.

Native Credits use Lumia's session data. Repeated activation uses the tested viewer compatibility bridge; populated session entries and separate-account import still need acceptance testing.

## Choose your chat panel

Named web chat panels are available in alpha 7 and later.

On **Show Control → Show**, the heading keeps its original plain white style. Click its text to cycle through native **Chat** and your saved panels, shown by the names you choose. Click the adjacent emblem and choose **Add chat…** to enter a **Name** and complete HTTP or HTTPS **Link**. You can add any compatible web chat page. Each panel keeps its own name, link and page size; editing one panel does not replace another. The page appears inside the main Chat card, which retains its normal 270 px minimum width.

With a saved panel selected, its emblem menu offers **Edit this chat…**, **Reload**, **Open in browser**, **Page size** and **Remove this chat panel**. Returning to **Chat** restores Pulse Weaver's usual feed and controls; its emblem offers the generic **Add chat…** action.

For example, name a panel **Lumia** and use `https://lumiastream.com/dock/chat` for its focused Chatbox. A new Lumia panel starts at **67%**; other new panels start at 100%. Saved sizes are retained independently. **Lumia view** options appear only when the selected panel's link uses the exact `lumiastream.com` or `www.lumiastream.com` host. They switch that panel between **Show Lumia chat only** and **Show full Control Room**. The native Chat menu provides the same generic add flow for every service.

External pages use the current app's shared browser context and retained login storage. They close when you return to **Chat** or leave the Show view; returning opens the remembered page with that storage intact. Sign in to a third-party service yourself when it asks. Pulse Weaver's connected chat events remain available to Action rules. Changing the displayed panel does not promise reduced YouTube receiving quota: the current incoming YouTube reader already uses web chat.

The older [theme builder guide](BUILD-MY-SHOW-GUIDE.md) records the previous builder and remains historical.
