#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ProjectHax LLC
#
# Sign a deployed nutty.app (after macdeployqt) and package it into a .dmg + .zip.
#
#   sign-macos.sh <nutty.app> <output-basename>
#       e.g. sign-macos.sh build/src/app/nutty.app nutty-macos-universal
#
# Two modes, chosen by whether MACOS_CERTIFICATE is set:
#
#   * Developer ID (proper distribution): imports the cert, signs every nested
#     binary inside-out with the hardened runtime (plugins and QML plugins, then
#     the Qt frameworks, then the app with its entitlements), then, if a notary
#     key is also set, notarizes with Apple and staples the ticket. The result
#     opens with no Gatekeeper warning. `codesign --deep` is avoided on purpose:
#     it signs nested code without the runtime/timestamp options a Qt bundle
#     needs, and notarization rejects it.
#
#   * No cert (default): ad-hoc signs (`codesign -s -`). The app launches (Apple
#     Silicon requires at least an ad-hoc signature), but downloaded copies still
#     hit Gatekeeper's "unidentified developer" prompt.
#
# macOS signing needs a paid Apple Developer Program membership. To enable the
# Developer ID path, set these GitHub Actions secrets:
#
#   MACOS_CERTIFICATE       base64 of a "Developer ID Application" .p12
#                           (Apple Developer portal -> Certificates -> Developer ID
#                            Application; export from Keychain as .p12, then
#                            `base64 -i cert.p12 | pbcopy`)
#   MACOS_CERTIFICATE_PWD   the .p12 export password
#   MACOS_SIGN_IDENTITY     e.g. "Developer ID Application: Example LLC (TEAMID)"
#   MACOS_NOTARY_KEY        base64 of an App Store Connect API key (.p8)
#                           (App Store Connect -> Users and Access -> Integrations ->
#                            Keys; role "Developer"; download the .p8 once)
#   MACOS_NOTARY_KEY_ID     the key's ID
#   MACOS_NOTARY_ISSUER_ID  the issuer ID shown on that Keys page
set -euo pipefail

app="${1:?usage: sign-macos.sh <app> <output-basename>}"
out="${2:?usage: sign-macos.sh <app> <output-basename>}"
tmp="${RUNNER_TEMP:-${TMPDIR:-/tmp}}"
# Apple Events (System Events "shut down") under the hardened runtime.
entitlements="$(cd "$(dirname "$0")/.." && pwd)/packaging/macos/nutty.entitlements"

[ -d "$app" ] || { echo "error: no such app bundle: $app" >&2; exit 1; }

is_macho() { file -b "$1" | grep -q 'Mach-O'; }

if [ -n "${MACOS_CERTIFICATE:-}" ]; then
  identity="${MACOS_SIGN_IDENTITY:?MACOS_SIGN_IDENTITY required with MACOS_CERTIFICATE}"
  echo "==> Signing with Developer ID: $identity"
  keychain="$tmp/nutty-signing.keychain-db"
  kc_pw="$(openssl rand -base64 24)"
  security create-keychain -p "$kc_pw" "$keychain"
  security set-keychain-settings -lut 21600 "$keychain"
  security unlock-keychain -p "$kc_pw" "$keychain"
  printf '%s' "$MACOS_CERTIFICATE" | openssl base64 -d -A -out "$tmp/cert.p12"
  security import "$tmp/cert.p12" -k "$keychain" -P "${MACOS_CERTIFICATE_PWD:-}" \
    -T /usr/bin/codesign
  rm -f "$tmp/cert.p12"
  security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$kc_pw" "$keychain" >/dev/null
  # Make the temp keychain searchable alongside the login keychain.
  security list-keychains -d user -s "$keychain" \
    "$(security default-keychain -d user | tr -d ' "')"

  sign() { codesign --force --options runtime --timestamp --sign "$identity" "$@"; }

  # 1. Every loose Mach-O: Qt plugins, QML plugins, stray dylibs.
  while IFS= read -r -d '' f; do
    is_macho "$f" && sign "$f"
  done < <(find "$app/Contents" -type f \( -name '*.dylib' -o -name '*.so' \) -print0)
  # 2. The Qt frameworks (signing the bundle signs its versioned binary).
  for fw in "$app"/Contents/Frameworks/*.framework; do
    [ -d "$fw" ] && sign "$fw"
  done
  # 3. Any other executable helpers, then the app itself with its entitlements.
  while IFS= read -r -d '' f; do
    [ "$f" = "$app/Contents/MacOS/nutty" ] && continue
    is_macho "$f" && sign "$f"
  done < <(find "$app/Contents/MacOS" -type f -perm -u+x -print0)
  sign --entitlements "$entitlements" "$app"
  codesign --verify --strict --deep --verbose=2 "$app"
else
  echo "==> No MACOS_CERTIFICATE: ad-hoc signing (Gatekeeper will warn on download)."
  codesign --force --deep --sign - "$app"
fi

# Notarization needs both a cert and a notary key.
notarize_enabled=""
if [ -n "${MACOS_CERTIFICATE:-}" ] && [ -n "${MACOS_NOTARY_KEY:-}" ]; then
  notarize_enabled=1
  printf '%s' "$MACOS_NOTARY_KEY" | openssl base64 -d -A -out "$tmp/notary.p8"
fi

# Submit one artifact (.zip or .dmg) and block until Apple accepts it. Each
# distributed artifact is notarized in its own right so `stapler staple` finds
# a ticket for it.
notarize() {
  xcrun notarytool submit "$1" \
    --key "$tmp/notary.p8" \
    --key-id "${MACOS_NOTARY_KEY_ID:?MACOS_NOTARY_KEY_ID required}" \
    --issuer "${MACOS_NOTARY_ISSUER_ID:?MACOS_NOTARY_ISSUER_ID required}" \
    --wait
}

# Fetch the ticket from Apple's ticket service and attach it. It drops the odd
# connection ("The network connection was lost", exit 68), so try a few times.
staple() {
  local i
  for i in 1 2 3 4 5; do
    xcrun stapler staple "$1" && return 0
    echo "==> Stapling failed (try $i of 5), waiting before the next..."
    sleep $((i * 10))
  done
  return 1
}

if [ -n "$notarize_enabled" ]; then
  echo "==> Notarizing $(basename "$app")..."
  ditto -c -k --keepParent "$app" "$tmp/notarize.zip"
  notarize "$tmp/notarize.zip"
  staple "$app"
fi

# DMG: the app next to an /Applications symlink, for the familiar drag-to-install
# layout. `ditto` keeps the signature and stapled ticket intact.
dmg_stage="$tmp/nutty-dmg-stage"
rm -rf "$dmg_stage"
mkdir -p "$dmg_stage"
ditto "$app" "$dmg_stage/$(basename "$app")"
ln -s /Applications "$dmg_stage/Applications"
hdiutil create -volname nutty -srcfolder "$dmg_stage" -ov -format UDZO "$out.dmg"
rm -rf "$dmg_stage"

ditto -c -k --keepParent "$app" "$out.zip"

if [ -n "$notarize_enabled" ]; then
  echo "==> Signing + notarizing the DMG..."
  codesign --force --timestamp --sign "$MACOS_SIGN_IDENTITY" "$out.dmg"
  notarize "$out.dmg"
  staple "$out.dmg"
  rm -f "$tmp/notary.p8"
fi
echo "==> Wrote $out.dmg and $out.zip"
