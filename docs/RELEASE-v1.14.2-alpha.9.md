# Pulse Weaver 1.14.2 alpha 9 — clearer setup and separate Look artwork

The layout wizard now assigns artwork separately for each Look and canvas. Countdown, Getting ready, BRB and Ending can use different Lumia templates within their shared Stage. Previously a group's Looks shared one artwork assignment. Generated Looks now show their own artwork and hide the others while preserving shared feed identities. Newly added browser overlays stay loaded while hidden so Look switches preserve their timer and widget state.

Extra overlays can target one selected Look, an entire selected stage group or all new layout Looks. This lets Newsroom appear only in its intended presentation. Deselecting an overlay's target prompts you to retarget or skip it instead of silently dropping the overlay.

Lumia template import links are rejected as browser URLs, with instructions to import the template and copy its browser link. Countdown minutes appear only for the built-in countdown; the guide explains that Lumia countdown duration comes from its start action. Setup guidance also explains when to add cameras and stingers, maps the published artwork families to their Looks, and makes clear that imported Fortnite helpers still need your own reward bindings and costs. The wizard initially sizes itself to the available screen.

Includes alpha 8's chat retention fix, companion plugin **1.4.4** and the existing published wizard template bundle. Existing shows, saved layout presets and older shared-artwork requests remain compatible.

Validation: native build and **1,736 isolated motion/wizard checks**, including separate BRB/Ending artwork on both canvases, visibility when switching Looks, overlay targeting, retained browser settings and transactional rejection of invalid assignments. Release assets contain application files and public templates, with no personal profiles, Lumia tokens, browser logins or local settings.

Enable **Studio → Updates → Include experimental alpha builds**, stop outputs and check for updates.
