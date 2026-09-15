#!/usr/bin/env bash
set -euo pipefail
[[ "$(uname -s)" == Darwin && "$(uname -m)" == arm64 ]] || { echo "Use an Apple Silicon macOS builder."; exit 1; }
project_root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$project_root"
release_tag="${PULSE_MAC_TAG:-mac-v0.1.0-alpha.1}"
[[ "$release_tag" =~ ^mac-v[0-9]+\.[0-9]+\.[0-9]+-alpha\.[0-9]+$ ]] || exit 1
out="$project_root/artifacts/macos/$release_tag"
[[ ! -e "$out" ]] || { echo "Release output already exists."; exit 1; }
mkdir -p "$out"
cd engine/obs-studio
cmake --preset pulse-macos-arm64
cmake --build build_macos --config Release --target obs-studio --parallel 3
cmake --install build_macos --config Release --component Application --prefix "$out/stage"
cd "$project_root"
app="$out/stage/Pulse Weaver Mac Preview.app"
[[ -d "$app" ]] || { echo "Application bundle is missing."; exit 1; }
exe="$app/Contents/MacOS/Pulse Weaver Mac Preview"
[[ "$(lipo -archs "$exe")" == arm64 ]] || { echo "Unexpected executable architecture."; exit 1; }
[[ "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$app/Contents/Info.plist")" == studio.pulseweaver.macpreview ]] || exit 1
[[ -d "$app/Contents/PlugIns/pulse-weaver-core.plugin" ]] || { echo "Pulse Weaver core is missing."; exit 1; }
[[ -d "$app/Contents/PlugIns/pulse-weaver-sample-clock.plugin" ]] || exit 1
[[ ! -d "$app/Contents/Library/SystemExtensions" ]] || { echo "Unexpected system extension."; exit 1; }
# Synthetic Keychain round-trip and isolated path tests on the actual Mac builder.
cmake -S tests/MacPreview -B tests/MacPreview/build -DCMAKE_PREFIX_PATH="$project_root/engine/obs-studio/.deps/obs-deps-qt6-2026-07-15-universal"
cmake --build tests/MacPreview/build --parallel 3
tests/MacPreview/build/PulseMacPreviewTests
codesign --force --deep --sign - --preserve-metadata=entitlements,requirements,flags "$app"
codesign --verify --deep --strict --verbose=2 "$app"
"$exe" --version
cp packaging/macos/README-MAC.txt "$out/stage/README-MAC.txt"
cp engine/obs-studio/COPYING "$out/stage/LICENSE.txt"
ln -s /Applications "$out/stage/Applications"
archive="PulseWeaver-Mac-$release_tag-AppleSilicon"
hdiutil create -volname "Pulse Weaver Mac Preview" -srcfolder "$out/stage" -ov -format UDZO "$out/$archive.dmg"
ditto -c -k --sequesterRsrc --keepParent "$app" "$out/$archive.zip"
git archive --format=tar HEAD | gzip > "$out/PulseWeaver-Mac-$release_tag-Source.tar.gz"
cp packaging/macos/README-MAC.txt "$out/README-MAC.txt"
cd "$out"
shasum -a 256 ./*.dmg ./*.zip ./*.tar.gz README-MAC.txt > SHA256SUMS.txt
shasum -a 256 -c SHA256SUMS.txt
