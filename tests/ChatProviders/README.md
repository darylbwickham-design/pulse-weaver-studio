# Show Chat provider checks

The preference suite uses the production chat-panel helper, Qt Core and temporary files. It does not initialize CEF, open URLs, contact a platform or read installed preferences. Its cases cover safe web links, multiple named panels, stable identities through edits, independent page sizes, name normalization, cycle/removal behavior, legacy conversion and authoritative schema-3 lists, plus lazy browser lifecycle decisions.

Configure with CMake and the bundled `Qt6_DIR`, build `RelWithDebInfo`, and run `PulseChatProviderTests` with the bundled Qt `bin` directory on `PATH`. In the Windows sandbox, point `TEMP` and `TMP` at a writable workspace directory so the temporary preferences file can be created. Verified 11 October 2026: **307 generic panel checks passed**.

Runtime acceptance uses an isolated Pulse Weaver configuration with outputs idle:

| Check | Expected result |
| --- | --- |
| Start with no provider preference and click the title repeatedly | Native Chat remains selected; no provider is added and no external browser is created. |
| Open the chat icon while native Chat is selected | Add chat is available; external-only editing, view, size and removal tools stay hidden. |
| Add a named panel with a web link | Its saved name appears in the title and its page replaces the main Show Chat body. |
| Add three named links, then click the title repeatedly | The title cycles native → each saved name in order → native, omitting removed entries. |
| Edit a panel's name and link, then restart | The same panel identity stays selected, the new title/link persist and its page size is retained. |
| Add a blank name with a valid link | A useful host-based name is suggested and saved; no fixed provider type is required. |
| Use keyboard navigation on the title and icon | Separate accessible controls let the user cycle panels or open configuration. |
| Return to native Chat | The same native message history, filter, draft and send destination remain. |
| Choose another provider with a valid HTTP/HTTPS fixture URL | The browser replaces the same Chat body; changing between external links navigates one embedded browser. |
| Select a panel named Lumia with a non-Lumia URL, then select a differently named real Lumia link | Lumia view options appear only for the exact Lumia host, independent of the saved name. |
| Enter a file, JavaScript, data or incomplete URL | The request is rejected without overwriting the last valid provider or link. |
| Remove the current external provider | Native Chat returns; removed providers no longer appear in the cycle. |
| Remove all external panels and restart with old deprecated keys present | The saved empty generic list remains native-only; historical keys do not add panels back. |
| Choose Lumia's full Control Room, then restart | The full view remains selected; an explicit choice is not migrated back to focused chat. |
| Add Lumia without an existing page-size preference | The normal-width panel uses 67%; an explicitly saved 100% remains 100%. |
| Reset Lumia to 100%, then choose Smaller three times; reset other chat and choose Larger twice; cycle and restart | Actual pages restore independent 75% and 125% sizes; native Chat stays unchanged. |
| Start with CEF's stored Lumia host zoom at 100% and the provider preference at 67% | After the main page finishes loading, the actual page uses 67%; the page-size label agrees with its rendered size. |
| Navigate directly from other chat at its unchanged 100% default to Lumia at 67% | The same browser widget restores 67% after Lumia's committed main-page load; the old host zoom does not override it. |
| Restart with a remembered external provider | Its browser appears only after the browser runtime is ready; the application remains usable during startup. |
| Switch repeatedly between native and external views | Stage/output state stays unchanged; native incoming event readers remain active for Action rules. |
| Leave Show or return to native Chat | The external browser is released; returning to an external view creates at most one browser. |
| Open an invalid or unavailable external page | Clicking the chat name still cycles to native Chat; the chat icon offers link configuration and Reload. |
| Switch providers rapidly while opening or closing a browser | The final selection appears; no second embedded browser is created during the CEF wait or close loop. |
| Exit with external chat open, then restart | Shutdown completes and the remembered panel returns after browser readiness. |

The runtime checks confirm display behavior and retention of native chat. External display does not disable native platform event reception or promise a reduction in API receive quota.
