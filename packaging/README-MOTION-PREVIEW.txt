PULSE WEAVER MOTION PREVIEW

This is an isolated preview build for testing native motion actions.

It installs to:
  %LOCALAPPDATA%\Programs\Pulse Weaver Motion Preview

It has a separate Start menu shortcut, uninstall entry, portable configuration,
update channel, local API port (18765), and Lumia plugin ID. It does not replace
the normal Pulse Weaver installation.

GETTING STARTED
1. Install PulseWeaver-Motion-Preview-Setup-1.13.0.exe.
2. Open the separate Pulse Weaver Motion Preview shortcut.
3. Open Action > Motion.
4. Choose NEW CLOSE-UP or NEW LAYOUT, use ordinary names, and save it.
5. Use RUN / PREVIEW ON OUTPUT. This changes the real program output.
6. Import PulseWeaver-Motion-Preview-Lumia-1.2.0.lumiaplugin in Lumia Stream
   to run the same saved action from Lumia or LumiCon.

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
