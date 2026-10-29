#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ProjectHax LLC
#
# Authenticode-sign nutty's Windows builds with Azure Trusted Signing.
#
# jsign is a pure-Java Authenticode signer, so this runs anywhere Java runs:
# from Linux (no Windows SDK) or on the Windows CI runner. It talks to Azure
# Trusted Signing via `--storetype TRUSTEDSIGNING` with a short-lived OAuth
# token, taken from TS_TOKEN or from the local `az` session (no stored secret).
#
#   scripts/sign-windows.sh nutty.exe [more.exe ...]
#       Sign PE files in place.
#
#   scripts/sign-windows.sh nutty-windows-x86_64.zip
#       Sign nutty.exe inside a portable zip, in place (re-zips it).
#
#   scripts/sign-windows.sh --jsign-command
#       Print the jsign command line (without the file) for Inno Setup's
#       SignTool, so ISCC signs the installer *and* its uninstaller:
#         ISCC /DSignToolName=jsign "/Sjsign=<that command> $f" ...
#
#   scripts/sign-windows.sh --release v0.1.0 [--no-upload]
#       Out-of-band fallback for releases built without CI signing: download
#       the Windows zips and installers from the GitHub release, sign them and
#       re-upload (needs `gh`). Note this signs the installer but cannot sign
#       the nutty.exe packed inside it; CI signing (release.yml) does both.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# --- jsign (fetched on first use) ----------------------------------------------
JSIGN_VERSION="${JSIGN_VERSION:-7.4}"
JSIGN_JAR="${JSIGN_JAR:-$repo_root/build/tools/jsign-$JSIGN_VERSION.jar}"

# --- Azure Trusted Signing (eastus -> eus endpoint) ------------------------------
# Defaults for the maintainer's signing account; override via the environment.
TS_ENDPOINT="${TS_ENDPOINT:-https://eus.codesigning.azure.net}"
TS_ACCOUNT="${TS_ACCOUNT:-projecthax}"
TS_PROFILE="${TS_PROFILE:-ProjectHax}"
TS_TSA="${TS_TSA:-http://timestamp.acs.microsoft.com}"
TS_TOKEN="${TS_TOKEN:-}"

log() { printf '%s\n' "$*" >&2; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

usage() {
    awk 'NR>3 && /^#/ {sub(/^# ?/, ""); print; next} NR>3 {exit}' "${BASH_SOURCE[0]}"
    exit "${1:-0}"
}

ensure_jsign() {
    command -v java >/dev/null 2>&1 || die "java not found: jsign needs a JRE."
    [[ -f "$JSIGN_JAR" ]] && return
    log "fetching jsign $JSIGN_VERSION -> $JSIGN_JAR"
    mkdir -p "$(dirname "$JSIGN_JAR")"
    curl -fsSL -o "$JSIGN_JAR" \
        "https://repo1.maven.org/maven2/net/jsign/jsign/$JSIGN_VERSION/jsign-$JSIGN_VERSION.jar" \
        || die "failed to download jsign"
}

ensure_token() {
    [[ -n "$TS_TOKEN" ]] && return
    command -v az >/dev/null 2>&1 || die "az (Azure CLI) not found, and TS_TOKEN is unset."
    log "requesting a Trusted Signing token from your az session..."
    TS_TOKEN="$(az account get-access-token --resource https://codesigning.azure.net \
        --query accessToken -o tsv 2>/dev/null)" || true
    [[ -n "$TS_TOKEN" ]] \
        || die "couldn't get a token: run \`az login\` with an account allowed to sign with '$TS_ACCOUNT'."
}

# The jar path as the platform's java sees it (Git Bash paths aren't Windows paths).
jar_path() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$JSIGN_JAR"; else printf '%s' "$JSIGN_JAR"; fi
}

jsign_args() {
    printf '%s ' java -jar "$(jar_path)" --storetype TRUSTEDSIGNING \
        --keystore "$TS_ENDPOINT" --storepass "$TS_TOKEN" \
        --alias "$TS_ACCOUNT/$TS_PROFILE" \
        --tsaurl "$TS_TSA" --tsmode RFC3161 --alg SHA-256
}

sign_pe() {
    local file="$1"
    ensure_jsign
    ensure_token
    log "signing $(basename "$file")..."
    java -jar "$(jar_path)" --storetype TRUSTEDSIGNING \
        --keystore "$TS_ENDPOINT" --storepass "$TS_TOKEN" \
        --alias "$TS_ACCOUNT/$TS_PROFILE" \
        --tsaurl "$TS_TSA" --tsmode RFC3161 --alg SHA-256 \
        "$file"
}

sign_zip() {
    local zip="$1" abs work
    [[ -f "$zip" ]] || die "no such file: $zip"
    command -v unzip >/dev/null 2>&1 || die "unzip not found"
    command -v zip >/dev/null 2>&1 || die "zip not found"
    abs="$(cd "$(dirname "$zip")" && pwd)/$(basename "$zip")"
    work="$(mktemp -d)"
    # shellcheck disable=SC2064
    trap "rm -rf '$work'" RETURN
    unzip -q "$abs" -d "$work"
    [[ -f "$work/nutty.exe" ]] || die "$zip has no nutty.exe at its root"
    sign_pe "$work/nutty.exe"
    rm -f "$abs"
    (cd "$work" && zip -q -X -r "$abs" .)
    log "re-zipped $zip with the signed exe"
}

sign_arg() {
    case "$1" in
        *.zip) sign_zip "$1" ;;
        *) [[ -f "$1" ]] || die "no such file: $1"; sign_pe "$1" ;;
    esac
}

release_mode() {
    local tag="$1" upload="$2" work f signed=()
    command -v gh >/dev/null 2>&1 || die "gh (GitHub CLI) not found"
    work="$(mktemp -d)"
    log "downloading Windows assets from release $tag..."
    gh release download "$tag" --dir "$work" \
        --pattern 'nutty-windows-*.zip' --pattern 'nutty-windows-*-setup.exe' \
        || die "no nutty-windows-* assets on release $tag"
    for f in "$work"/nutty-windows-*.zip "$work"/nutty-windows-*-setup.exe; do
        [[ -e "$f" ]] || continue
        sign_arg "$f"
        signed+=("$f")
    done
    [[ ${#signed[@]} -gt 0 ]] || die "found no Windows assets to sign on $tag"
    if [[ "$upload" == "yes" ]]; then
        log "re-uploading signed assets to $tag..."
        gh release upload "$tag" "${signed[@]}" --clobber
    else
        for f in "${signed[@]}"; do mv -f "$f" "$(pwd)/$(basename "$f")"; log "signed: $(basename "$f")"; done
    fi
    rm -rf "$work"
}

main() {
    [[ $# -eq 0 ]] && usage 1
    case "$1" in
        -h|--help) usage 0 ;;
        --jsign-command)
            ensure_jsign
            ensure_token
            jsign_args
            echo
            ;;
        --release|--tag)
            local tag="${2:-}" upload="yes"
            [[ -n "$tag" ]] || die "--release needs a tag, e.g. --release v0.1.0"
            shift 2
            [[ "${1:-}" == "--no-upload" ]] && upload="no"
            release_mode "$tag" "$upload"
            ;;
        -*) die "unknown option: $1 (try --help)" ;;
        *)
            for arg in "$@"; do sign_arg "$arg"; done
            log "done. Verify on Windows with: signtool verify /pa nutty.exe"
            ;;
    esac
}

main "$@"
