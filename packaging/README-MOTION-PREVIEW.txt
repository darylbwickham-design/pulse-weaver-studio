PULSE WEAVER MOTION PREVIEW

This is an isolated preview build for testing native motion actions.

It installs to:
  %LOCALAPPDATA%\Programs\Pulse Weaver Motion Preview

It has a separate Start menu shortcut, uninstall entry, portable configuration,
update channel and local API port (18765). It does not replace the normal Pulse
Weaver installation. The companion Lumia package updates the existing Pulse
Weaver plugin and keeps its existing plugin identity.

GETTING STARTED
1. Install PulseWeaver-Motion-Preview-Setup-1.13.0.exe.
2. Open the separate Pulse Weaver Motion Preview shortcut.
3. Open Action > Motion.
4. Choose NEW CLOSE-UP or NEW LAYOUT. Click sources directly on the live canvas;
   for a close-up, drag the crosshair over the subject and use the zoom slider.
5. Use an ordinary action name and save it.
6. Use RUN / PREVIEW ON OUTPUT. This changes the real program output.
7. Import PulseWeaver-Lumia-1.2.0.lumiaplugin to update your existing Pulse
   Weaver plugin. Existing Lumia and LumiCon alert bindings are retained.
8. Set that plugin's Pulse Weaver port to 18765 while testing this preview.

STREAM DECK
Every saved action registers an OBS hotkey named "Pulse Weaver Motion: <name>".
Assign a key in Pulse Weaver's Hotkeys settings, then use Stream Deck's normal
Hotkey action. The button still runs through the same Stage guard and restore
logic as the native and Lumia buttons.

MOVE IMPORT
Use IMPORT MOVE / LUMIA JSON in Motion. Imported actions marked REVIEW remain
disabled until you inspect and save them. Absolute Move Source transforms and
Lumia Layout Studio states are converted. Relative math, filter choreography,
audio/media actions, trigger chains and exact easing are reported for manual
review instead of being guessed.

SAFETY
The preview only controls sources selected in each action. A manual Stage change
stops the action and restores its sources. Repeating a close-up extends its hold.
STOP + RESTORE returns controlled sources to their captured starting state.
