# Build a show in Pulse Weaver

This guide applies to Pulse Weaver 1.14 experimental alpha 3 and unstable 10. A new setup opens at **Settings → Start here**. Existing setups continue to open on Show.

1. Open **Show Control** and click **Build my show…** beside the Stage selector on a fresh install. If you already have stages, click **Control** in the Show heading, then **Build my show…**. You can start with an empty scene collection.
2. Name the show and tick the stages you want: Starting, Hangout, Gameplay, BRB/Ending, and Raid/Shoutout. Click **Next**.
3. For each role, select an **existing source**, click **Create new…**, or choose **I don't use this**. Set up the main camera, secondary focus (such as a printer), optional alert or pixel-board camera, Game Capture, and screen capture. A new Game Capture can target a running window or any fullscreen game. The unstable builder offers an existing scene item's camera crop and rotation as a starting frame. Click **Next**.
4. Assign landscape and portrait chat, alerts, captions, and stage graphics the same way. **Create new…** accepts a browser overlay URL for chat, alerts, or captions. Starting, BRB, Ending, and Raid each get a ready-to-use local Pulse Weaver title graphic unless you select an existing source. If an external app controls a browser overlay's placement, its full-canvas framing stays intact. Click **Next**.
5. Check that each overlay is assigned to the intended canvas. A newly created portrait chat source uses the portrait canvas size; chat and alerts that another app positions stay full canvas.
6. Choose desktop and microphone devices on **Set up sound**. **Keep current device** preserves an existing collection. Optionally select a dedicated music audio source to exclude from YouTube and Kick. Twitch and its VOD track stay as configured. Never select the same Game Capture source as the picture and music, because excluding it would also exclude the picture.
7. Choose **Main + corner**, **Side by side**, or **Fullscreen focus**. The portrait layout fills its lower two thirds with the main source and uses the upper third for the supporting source. Choose a transition for switching stages, including an existing stinger if one is listed. Click **Next**.
8. Review the stages and source assignments. Resolve any missing-source messages, then click **Finish**. Only now does the builder create the requested sources, landscape and portrait scenes, and saved looks. It removes what it just created if this step fails, and keeps your wizard choices available to correct the issue. Your existing scenes remain available.
9. In **Control**, choose a Stage and Look using the blue text choices above the previews, then click **Edit selected look**. Choose a layer on the right or click a preview, arrange it, and click **Save look**. Use **+ New look** for a fresh arrangement or **Duplicate look** to start from the selected one. **Choose sources + layout…** lets you pick the large and supporting sources explicitly, then preview **Main + corner**, **Side by side** or **Full screen main** on both canvases. Click **Undo** if it is wrong. **Swap focus** exchanges the two main sources. You can also drag a source to move it; drag a corner to resize; use **Shift** with a corner to stretch or **Alt** with a corner to crop. The right rail has **Fill**, **Corner**, **Left** and **Right** placements for an individual source.
10. Click **Run saved look live** to send the look to output. Switching to another Stage uses its fade, cut or stinger and reveals the finished look. Changing looks while staying in one Stage animates the sources into their saved positions. The existing Lumia **Run Stage Look / Motion Action** action can trigger saved looks.

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
