# 1.5.2

- Bundled the complete Pulse Weaver technical manual as an offline HTML reading edition and a page-numbered PDF print edition.
- Added **Open Pulse Weaver Manual**, an action that opens the selected edition while the plugin is connected. Both files remain available in the installed package for offline reading.
- Linked the manual from the plugin's setup guide, action reference, tutorial and README.
- Retained the plugin ID, existing action/alert IDs, saved field keys, alert defaults and variable names.

# 1.5.1

- Rewrote the public tutorial, connection help and action reference for Pulse Weaver 2.0 for Windows; testing used Lumia Stream 9.3.4.
- Clarified that the port must match separately from automatic token discovery, and that configured keypad alerts must be enabled.
- Replaced personal show examples and preview-build instructions with reusable Stage, Look and keypad walkthroughs.
- Added website download, GitHub and Discord support/contact links to the setup and tutorial materials.
- Clarified temporary-Look return limits, action completion, shared-source audio controls and troubleshooting.
- Documentation and help text update only. Plugin behaviour, manifest ID, action/alert IDs, saved field keys, alert defaults and variable names are unchanged.

# 1.5.0

- Public display name: **PulseWeaver Stream Studio**. The existing `pulseweavermotionpreview` manifest ID, action IDs, field keys and variable prefix are retained.
- Added **Keypad 01–Keypad 15** alerts for Pulse Weaver's on-screen keypad, including the button name and slot number.
- Added keypad capability negotiation so older plugins are not mistaken for a working keypad connection.
- Added setup and action guides and clarified the plugin's role as a Lumia/LumiCon integration for Pulse Weaver.
- Preserved local pushed events, reconnect behaviour and existing Stage/Look, output, source, audio and media controls. Keypad alerts default to Off.
