#!/usr/bin/env bash
# WSL/Linux launcher used by CrashVault.vbs (silent) and CrashVault.bat (diagnostic).
set -euo pipefail

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APPIMAGE="$DIR/src-tauri/target/release/bundle/appimage/CrashVault_1.0.0_amd64.AppImage"
BINARY="$DIR/src-tauri/target/release/crashvault-desktop"

export DISPLAY="${DISPLAY:-:0}"

# Prefer the direct Tauri binary for local/WSL launches.
# The AppImage produced by linuxdeploy currently ships WebKitWebProcess but omits
# injected-bundle/libwebkit2gtkinjectedbundle.so. Under WSL that causes WebKit to
# look for ././/lib/.../libwebkit2gtkinjectedbundle.so and the GUI fails to render
# even though the process may stay alive on the taskbar.
if [[ -x "$BINARY" ]]; then
  exec "$BINARY"
fi

if [[ -x "$APPIMAGE" ]]; then
  exec "$APPIMAGE" --appimage-extract-and-run
fi

echo "CrashVault not built yet. Run: cd desktop && npm run tauri:build" >&2
exit 1
