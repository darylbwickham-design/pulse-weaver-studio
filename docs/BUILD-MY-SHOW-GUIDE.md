# Build a show in Pulse Weaver

This guide describes the 1.14.2 theme builder candidate. A fresh setup opens at **Settings → Start here**; existing setups open on Show.

## Create Stages and animated Looks

1. Open **Show Control → Control → Build my show…**. Name your show and tick the themes you want. Each theme creates **one Stage** with a landscape scene and its paired portrait scene.
2. Click **Next**. Each Stage has **three Looks selected by default**, shown together as composition cards. Use the Stage tabs to inspect them; untick any view you do not need. Looks are arrangements of shared sources inside a Stage.
3. On **Assign sources**, use the Stage tabs. Choose an existing source from each dropdown or **Create new…**. The optional reuse checkbox fills empty assignments of the same capture role in other Stages; you can change individual assignments afterwards. Presenter and work/activity cameras are separate roles. Title/countdown graphics are prepared automatically, and can be replaced with an existing graphic or a browser URL. New sources are created only when you finish.
4. On **Add overlays**, assign up to **three full-canvas layers per canvas**. Each slot can use an existing source or a new browser URL. **Choose Looks…** controls where a layer is visible. Layer 1 is lowest and layer 3 highest. If your alert overlay already contains captions, use it for captions as well. Assign only the sticker service you intend to use. Personal provider URLs belong in your local profile, not the shipped templates.
5. On **Review both canvases**, select a target Look. Choose another Look under **Move from**, then **Play movement** or scrub the slider. The rehearsal uses the same movement and layer coverage code as live Looks, with private scenes. Browser widgets and uncreated sources appear as placeholders during this step. Rehearsing does not change output.
6. Choose **Adjust landscape** or **Adjust portrait**. Select **Frame source** to change its panel's Left, Top, Width and Height, fit/crop mode, and crop position. **Reset** restores that panel's template placement. The portrait divider controls the camera-above/content-below split; the corner control applies to landscape inset Looks. Changes apply to the selected Look and canvas. Movement duration applies to both canvases and defaults to **850 ms**.
7. Choose Fade or Cut for switching Stages. Stop streaming and recording before **Finish**. The builder validates assignments, creates the selected Stages and Looks, preserves existing source properties/audio/routing, and rolls back newly created resources if creation fails. Wizard choices remain available to correct the problem.
8. In **Control**, select a Stage and Look, then **Edit selected look**. Drag layers or use **Choose sources + layout…**, **Swap focus**, and the layer placement controls. **Save look** stores edits; **+ New look** and **Duplicate look** let you add further variations.
9. **Run saved look live** applies a saved Look. Staying in one Stage animates sources into their new framing. Switching Stages uses that Stage's transition and reveals the already prepared Look. Lumia's **Run Stage Look / Motion Action** can trigger the same saved Looks.

## The three starting compositions

| Stage | Look 1 | Look 2 | Look 3 |
| --- | --- | --- | --- |
| Starting | Countdown focus: small dimmed camera left | Camera teaser: camera grows left | Ready to begin: camera grows again, countdown moves beside it |
| Viewer focus | Just chatting: full presenter | Screen reaction: screen leads, small left presenter | Presenter reaction: large presenter left, smaller screen right |
| Gameplay focus | Gameplay focus: game with optional small camera | Balanced gameplay: larger left camera column, game right | Presenter reaction: large presenter left, smaller game right |
| Intermission | BRB focus: holding message with optional activity | Return countdown: larger activity column | Activity continues: full activity with holding label |
| Craft focus | Work focus: work with optional small camera | Balanced work: larger left presenter, work right | Presenter explains: large presenter left, smaller work right |
| Wrap-up | Presenter goodbye: full camera with closing message | Credits focus: camera shrinks left | Final card: message leads, camera moves to a small inset |

Shared sources keep the same scene item identity so changing Looks can move them. Starting uses one scoped dimmed/clipped-corner camera wrapper per canvas, and one countdown browser per canvas, shared across its three Looks. The original camera remains unfiltered. Portrait camera/content layouts have matching widths and no gap, with different split heights for different Looks. Game and screen images fit their panels by default; crop is an explicit choice.

Chat is an overlay throughout, never the main content panel. Lumia Stream's **Chatty** and **Chatty vert** sources use the full landscape and portrait canvases respectively. Put them in overlay layer 3, visible in every Look, above the alert and sticker layers.

When a small left presenter grows into a column, content stays underneath until the camera covers that column. The reverse move fills the canvas before shrinking the camera. Control, wizard rehearsal and live Looks use the same movement sequence.

## Copy an existing show

Choose **Copy an existing show's compositions…** on the first builder page to use the reference workflow. Name the new show, select reference Stages, review, and finish. It retains saved Looks, layers, camera crops, independent landscape/portrait framing, movement durations, output assignments, sound exclusions, and Stage transitions.

The copied scenes and Looks have their own identities. Sources and nested group contents remain shared: source-property changes affect every use, while moving a top-level layer changes only its composition.

## Understand Control and live output

**Stage** chooses the scene and destination routing. **Look** saves the end positions, visibility and layer order of sources in that Stage. Clicking a Stage or Look name in Control loads it into the design canvases; it does not change the broadcast. Editing a canvas changes only the draft until you click **Save look**.

To rehearse the pictured Gameplay transition without touching output:

1. In Control, choose **Gameplay** and then **Printer activity**.
2. Under **Preview from**, choose **Screen activity**. This gives the preview a different saved starting arrangement. Choose **Current scene state** only when you want to compare with the scene's present placement.
3. Click **Play look movement** or drag its slider. The two design canvases simulate the source movement. If the start and end placements already match, Control says why nothing moved.
4. To perform it live later, keep the Gameplay Stage active, run **Screen activity**, then run **Printer activity**. The source movement uses the duration under **Settings → Movement** in Control. Save any draft edits before running a look live.

If you run Printer activity from a different Stage, the Stage's transition takes priority and reveals the prepared Printer activity layout. You will not see the same within-Stage source movement after that Stage switch.

## Change the captured game

1. On the main **Show** page, find **Game window** below the Stage selector.
2. Click the gear and select your Game Capture source. If it uses another capture mode, choose **Use Capture specific window** from the same gear menu.
3. Choose the game from the **Game window** dropdown. If the game opened after the page, use **Refresh game windows** in the gear menu.

The picker changes only the selected Game Capture source's target window. Its audio, scene placement, and output routing remain under the existing source and output controls.

## Finish sound, video and recording settings

1. Open **Settings → Sound and recording**. Choose desktop and microphone devices under **Audio devices**, then click **Save audio devices**. Stop outputs before changing devices. **Refresh available devices** detects devices connected since opening Settings.
2. Under **Recording**, choose an existing folder and a container, then click **Save recording choices**. MKV is recoverable if a recording ends unexpectedly. The folder and format apply to the next recording.
3. Open **Settings → Sources and canvas**. Check the base canvas, output size and frame rate. Click **Save canvas and frame rate** if changing them, restart Pulse Weaver, then review landscape and portrait looks.
4. In **Settings → Destinations**, check the Kick and YouTube bitrates and save them for the next output start. Kick has a landscape bitrate only. Choose the actual live destinations on Show.
5. Under **Twitch VOD track**, enable the separate track and choose its track number when using Advanced output. Simple output uses track 2. Under **Audio track names**, give the tracks useful names, such as Live, VOD and Recording, then save.
6. Under **Audio monitoring**, choose the device on which you hear locally monitored sources. Under **Audio source routing**, tick which of the six tracks contains each source, choose Off, Monitor only, or Monitor + output, and click **Save audio source routing**. The matrix shows your saved track membership even when a stage temporarily uses other tracks for provider exclusions. These checks determine which sources reach the Twitch VOD track; they do not change YouTube or Kick source exclusions. Stop outputs before saving and check the mixer before going live.
7. Advanced encoders and custom FFmpeg recording still use **Advanced OBS options**. Keep the current settings unless you intend to change the relevant output profile.

## If a source is missing

Return to its wizard step and use **Create new…**, or add a source in **Camera** and reopen the builder. A browser source still needs the URL from your chat, alert, or caption provider. The wizard creates the browser source but does not create an external account. Choose a different show name if one of the new Stage names is already in use.
