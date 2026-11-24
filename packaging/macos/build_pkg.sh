#!/usr/bin/env bash
# Builds a macOS installer package from a Release build.
#   packaging/macos/build_pkg.sh <build-dir> <version> [developer-id-installer] [developer-id-app]
# Without Developer IDs the plug-ins are ad-hoc signed (required on Apple Silicon) and the
# .pkg is unsigned: Gatekeeper then asks the user to right-click > Open the first time.
# With IDs, binaries are signed with the hardened runtime and the .pkg can be notarized:
#   xcrun notarytool submit MysteryBox-<v>-macOS.pkg --keychain-profile <profile> --wait
#   xcrun stapler staple MysteryBox-<v>-macOS.pkg
set -euo pipefail
BUILD="$1"; VERSION="$2"; INSTALLER_ID="${3:-}"; APP_ID="${4:--}"
ART=$(find "$BUILD" -type d -path "*MysteryBox_artefacts/Release" | head -1)
STAGE=$(mktemp -d)
mkdir -p "$STAGE/Library/Audio/Plug-Ins/VST3" "$STAGE/Library/Audio/Plug-Ins/Components" "$STAGE/Applications"
cp -R "$ART/VST3/Mystery Box.vst3" "$STAGE/Library/Audio/Plug-Ins/VST3/"
cp -R "$ART/AU/Mystery Box.component" "$STAGE/Library/Audio/Plug-Ins/Components/"
cp -R "$ART/Standalone/Mystery Box.app" "$STAGE/Applications/"
for b in "$STAGE/Library/Audio/Plug-Ins/VST3/Mystery Box.vst3" "$STAGE/Library/Audio/Plug-Ins/Components/Mystery Box.component" "$STAGE/Applications/Mystery Box.app"; do
  if [ "$APP_ID" = "-" ]; then codesign --force --deep -s - "$b"; else codesign --force --deep --options runtime --timestamp -s "$APP_ID" "$b"; fi
done
OUT="MysteryBox-$VERSION-macOS.pkg"
if [ -n "$INSTALLER_ID" ]; then
  pkgbuild --root "$STAGE" --install-location / --identifier com.raygun.mysterybox --version "$VERSION" --sign "$INSTALLER_ID" "$OUT"
else
  pkgbuild --root "$STAGE" --install-location / --identifier com.raygun.mysterybox --version "$VERSION" "$OUT"
fi
(cd "$STAGE" && zip -qry "$OLDPWD/MysteryBox-$VERSION-macOS-plugins.zip" Library Applications)
echo "Built $OUT and MysteryBox-$VERSION-macOS-plugins.zip"
