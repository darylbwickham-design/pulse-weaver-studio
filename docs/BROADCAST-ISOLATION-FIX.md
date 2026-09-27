# Broadcast setup isolation fix

## Confirmed defects
- Native OBS broadcast setup trusted the loaded account type even after Pulse Weaver switched its primary output to Twitch. A stale YouTube account could therefore demand YouTube setup for a Twitch/Kick show.
- Four example Friday Fortnite schedules were embedded in the native YouTube dialog. Failed initialization could reopen that dialog before those placeholders were removed. These were UI samples, not a user's downloaded schedule.
- Related stop warnings and startup polling also used stale broadcast state. A failed account load remained attached, and a cancelled primary start could be reported as accepted.

## Changes
- Gate native YouTube setup, warnings and polling on both the actual primary service and the connected account. Preserve native YouTube setup when selected.
- Remove sample schedule rows, refuse to open an invalid dialog, guard missing docks/accounts, and escape broadcast titles rendered as rich text.
- Make worker cancellation atomic and safely remove the loading label.
- Stop secondary startup after an immediately cancelled/rejected primary start; distinguish an output still connecting from cancellation.
- Retain existing credentials, ports, profiles, routing and streaming encoders. No new scopes or reconnection required for this fix.

## Verification and limits
Regression tests cover production broadcast eligibility, valid empty schedule UI resources, update channel selection and updater handoff. Application builds and packaged installers are checked separately. No live broadcasts or installed user instances are used for testing. The reporter's exact runtime state has not been reproduced on their machine.

## Update
Install through Studio > Updates or download the matching installer from this release. Review the selected destinations before going live. With YouTube off and Twitch/Kick selected, no YouTube broadcast setup should be requested. Existing account settings should remain intact.