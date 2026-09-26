# Pulse Weaver 1.13.1 alpha 4

This alpha makes Show Control easier to set up and use on a new scene collection.

- **Build my show** now works from an empty collection. Each role can use an existing source or create a new camera, Game Capture, display/window capture, browser overlay, or local stage title graphic. It asks for the main camera, secondary focus, optional alert camera, game, screen, landscape and portrait chat, alerts, captions, and stage graphics. It then builds Starting, Hangout, Gameplay, BRB/Ending, and Celebration with landscape and portrait scenes and saved looks. New sources are created only at Finish and rolled back with the scenes if creation fails. Existing scenes remain available. Main content fills the portrait lower area; supporting content uses the upper area. Stage graphics sit above their background sources.
- **Control** has compact stage and look choices above the two canvases. **Swap focus** exchanges the large and supporting source while retaining each source's crop, so a camera/content pair can become a second look quickly. Review the draft, then save it.
- The main **Show** page has a Game window picker. Its gear chooses an existing Game Capture source, switches that source to specific-window mode when requested, and refreshes OBS's available windows. The selected source is remembered by UUID.
- Creating a Stage manually now starts with current or selected scene assignments, so the new Stage is usable instead of an empty row.
- The builder now saves the Stage catalogue on Windows, refreshes saved Game Capture choices after startup, finds the paired portrait editor scene immediately after creating a stage, creates portrait-sized title graphics to keep their text readable, and puts corner sources and graphics above opaque full-screen content.

See [Build a show](BUILD-MY-SHOW-GUIDE.md) for the click-by-click setup path.

The alpha was built and checked in a separate portable profile using temporary colour, Game Capture, and newly generated stage graphic sources. No live stream, viewer moderation, account connection, or installer run was used for this check. Existing streaming routes, credentials, ports, and Lumia action IDs are unchanged. The alpha updater retains the installed profile and makes its normal verified backup; users may update from the in-menu alpha channel.
