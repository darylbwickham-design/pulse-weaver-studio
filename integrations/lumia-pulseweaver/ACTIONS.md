# Use your Pulse Weaver show from Lumia

## Stages and Looks

Use **Activate Stage** to select an existing Stage, or **Run Stage Look / Motion Action** for a saved Look. **Undo Previous Stage / Look** returns to the remembered presentation when an exact return point is available. Use **Remember Current Stage / Look** first when several temporary changes should share one return point.

## Outputs, sources and audio

Start/end the saved show or operate individual Twitch, Kick and YouTube destinations. Recording is independent. Source visibility targets a specific scene item; source mute and volume affect that source everywhere it is used. Configure platform-specific audio exclusions inside Pulse Weaver.

## On-screen buttons

Each **Keypad 01–Keypad 15** alert corresponds to the same numbered button in Pulse Weaver's chat-panel keypad. Add the desired Lumia reactions to that alert. The alert supplies `buttonIndex`, `buttonName` and `message` for your reaction templates.

For example, name button 01 **Gameplay**, then put **Activate Stage → Gameplay** into the **Keypad 01** alert's reactions. The button label can later change without breaking the binding.

The keypad runs Lumia alerts. It does not bypass Lumia or edit scene/source definitions. No old button presses are replayed after reconnecting.
