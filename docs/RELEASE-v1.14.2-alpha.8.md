# Pulse Weaver 1.14.2 alpha 8 — chat panels stay loaded

Switching from a web chat panel to native Chat, another saved panel or another studio tab now preserves the visited page. Returning keeps its draft and scroll position instead of recreating the browser. This also keeps Lumia's focused Chatbox or Control Room loaded while you use other Pulse Weaver views.

Each visited panel retains its own browser and saved page size. **Reload** refreshes only the selected panel; removing a panel closes its page. Closing Pulse Weaver releases all retained pages. The existing plain white heading and generic chat icon menu remain the controls for switching and configuring panels.

Includes alpha 7's named chat panels, portrait wizard fixes, Lumia companion **1.4.4** and published wizard templates. Native chat events remain available for Action rules. Retained pages can continue receiving updates in the background and consume resources while the app is open.

Enable **Studio → Updates → Include experimental alpha builds**, stop outputs and check for updates. The installer preserves existing configuration through the established backup and recovery flow. No personal profiles, Lumia tokens, browser logins, camera settings or local connection settings are included.

Validation: native build, **311 ChatProviders checks**, and isolated runtime checks preserving a draft, scroll position and Lumia's page instance across panel and tab switches. Camera decoding warnings are not fixed in this release.
