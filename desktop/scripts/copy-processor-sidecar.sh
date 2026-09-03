#!/usr/bin/env bash
# Copy the native crashvault-process binary into the Tauri sidecar location.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEST_DIR="${SCRIPT_DIR}/../src-tauri/binaries"
TARGET="${DEST_DIR}/crashvault-process-x86_64-unknown-linux-gnu"

SRC="${CRASHVAULT_PROCESSOR_BIN:-}"
if [[ -z "${SRC}" ]]; then
  for candidate in \
    "${SCRIPT_DIR}/../../build/crashvault-process" \
    "/tmp/crashvault-build/crashvault-process"; do
    if [[ -x "${candidate}" ]]; then
      SRC="${candidate}"
      break
    fi
  done
fi

if [[ -z "${SRC}" || ! -x "${SRC}" ]]; then
  echo "error: crashvault-process not found. Build native project first or set CRASHVAULT_PROCESSOR_BIN." >&2
  exit 1
fi

mkdir -p "${DEST_DIR}"
cp "${SRC}" "${TARGET}"
chmod +x "${TARGET}"
echo "Sidecar ready: ${TARGET}"
