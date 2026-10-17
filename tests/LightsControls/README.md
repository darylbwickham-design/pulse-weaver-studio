# Lights controls checks

The Qt test compiles the production click-through navigator, provider request builder and passive native-audit helper. It uses hidden widgets and in-memory device/look models. It checks device-card selection, Back navigation, saved-look loading, hidden authoritative models, native card sizing, request methods/payloads and Hue error responses. It creates no network manager, performs no discovery and sends no lighting commands.

```powershell
cmake -S tests/LightsControls -B build/lights-controls -DCMAKE_PREFIX_PATH="<Qt root>"
cmake --build build/lights-controls --config Release --parallel 2 -- /nr:false
$env:PATH = "<Qt root>/bin;" + $env:PATH
$env:QT_PLUGIN_PATH = "<Qt root>/plugins"
ctest --test-dir build/lights-controls -C Release --output-on-failure
```

For Windows theme validation, set `QT_QPA_PLATFORM=windows` and `PULSEWEAVER_CONTROL_THEME_QSS` to the native audit's `active-theme.qss`, then run the test executable. Its window remains hidden.

The separate native smoke fixture seeds a TEST-NET address, an encrypted fake credential and one saved look in its own portable configuration. It mounts the real Lights surface, selects only that synthetic card, verifies the masked credential/entity restore, loads the saved look locally and returns to device cards. It does not click Add/Test, Discover, Apply, Save or Remove. This confirms native mounting and local UI behaviour; physical lighting hardware remains untested.

Provider construction follows the official [WLED JSON API](https://kno.wled.ge/interfaces/json-api/), [Hue v1 getting-started guide](https://developers.meethue.com/develop/get-started-2/) and [HTTPS requirement](https://developers.meethue.com/), and [Home Assistant light action](https://www.home-assistant.io/actions/light.turn_on/) and [REST service API](https://developers.home-assistant.io/docs/api/rest/). Hue uses HTTPS PUT with normal Qt certificate/name validation; bridge certificate provisioning or custom trust/pinning is not implemented. A bridge reached by raw IP may fail TLS validation. HTTP / DIY and Display / Pixel device retain the existing generic JSON endpoint contract; they are not new device SDK integrations.
