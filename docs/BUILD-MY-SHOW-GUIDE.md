# Build a show in Pulse Weaver

This guide applies to the experimental Show Control builder in 1.13.1 alpha 5.

1. Open **Show Control** and click **Build my show…** beside the Stage selector on a fresh install. If you already have stages, click **Control** in the Show heading, then **Build my show…**. You can start with an empty scene collection.
2. Name the show and tick the stages you want: Starting, Hangout, Gameplay, BRB/Ending, and Raid/Shoutout. Click **Next**.
3. For each role, select an **existing source**, click **Create new…**, or choose **I don't use this**. Set up the main camera, secondary focus (such as a printer), optional alert or pixel-board camera, Game Capture, and screen capture. A new Game Capture can target a running window or any fullscreen game. Click **Next**.
4. Assign landscape and portrait chat, alerts, captions, and stage graphics the same way. **Create new…** accepts a browser overlay URL for chat, alerts, or captions. Starting, BRB, Ending, and Raid each get a ready-to-use local Pulse Weaver title graphic unless you select an existing source. If an external app controls a browser overlay's placement, its full-canvas framing stays intact. Click **Next**.
6. Choose **Main + corner**, **Side by side**, or **Fullscreen focus**. The portrait layout fills its lower two thirds with the main source and uses the upper third for the supporting source. Choose a transition for switching stages, including an existing stinger if one is listed. Click **Next**.
7. Review the stages and source assignments. Resolve any missing-source messages, then click **Finish**. Only now does the builder create the requested sources, landscape and portrait scenes, and saved looks. It removes what it just created if this step fails. Your existing scenes remain available.
8. In **Control**, choose a stage and look using the blue text choices above the previews. Click a preview to edit it. Drag a source to move it; drag a corner to resize; use **Shift** with a corner to stretch or **Alt** with a corner to crop. **Swap focus** exchanges the two main sources in both canvases. Check both previews, then click **Save look**.
9. Click **Apply saved look** to run the look on output. Switching to another stage uses the stage transition; changing looks within one stage uses the saved motion. The existing Lumia **Run Stage Look / Motion Action** action can trigger saved looks.

## Change the captured game

1. On the main **Show** page, find **Game window** below the Stage selector.
2. Click the gear and select your Game Capture source. If it uses another capture mode, choose **Use Capture specific window** from the same gear menu.
3. Choose the game from the **Game window** dropdown. If the game opened after the page, use **Refresh game windows** in the gear menu.

The picker changes only the selected Game Capture source's target window. Its audio, scene placement, and output routing remain under the existing source and output controls.

## If a source is missing

Return to its wizard step and use **Create new…**, or add a source in **Camera** and reopen the builder. A browser source still needs the URL from your chat, alert, or caption provider. The wizard creates the browser source but does not create an external account. Choose a different show name if one of the new Stage names is already in use.
