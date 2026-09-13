# Public Dist 0.02

Changing stages while a cached stinger is running no longer stores the transition as its own source. Restarting settles the previous destination before starting the next stage. The engine also rejects a transition targeting itself.

Audio monitoring duplication now uses a weak source reference, retained safely for each audio render, to prevent accessing a released source during shutdown.

Lumia End Stream uses explicit platform stop actions. It cannot invoke the Go Live toggle. The included Lumia plugin 1.1.2 cancels stale stop cleanup when a newer start arrives. Alerts remain off by default and the P logo is included.

This remains a public testing preview. No developer registrations, signed-in accounts or personal configuration are included. Enter your own platform app details in Action → Connections.

Validation: native build passed; 2,100 interrupted transitions across seven route fixtures passed, with cuts and clean destruction; the previous engine fails the same regression. Audio lifetime test passed 1,500 source retirements and five shutdown cycles. Nine Lumia tests passed. Installer payload and layout tests passed. These fixtures do not start real streams or reproduce a particular user's stinger media.
