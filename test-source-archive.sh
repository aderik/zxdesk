#!/bin/bash
# Check a committed or staged tree without Docker or external BIOS ROMs.
# Usage: bash ./test-source-archive.sh [tree-ish, default HEAD]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
ARCHIVE_DIR="$(mktemp -d)"
trap 'rm -rf "$ARCHIVE_DIR"' EXIT

git -C "$ROOT" archive "${1:-HEAD}" | tar -x -C "$ARCHIVE_DIR"
if [[ -e "$ARCHIVE_DIR/roms" || -L "$ARCHIVE_DIR/roms" ]]; then
  echo 'FAIL: roms must remain local, outside Git' >&2
  exit 1
fi

# Match test.sh's input archive step in a checkout without external ROMs.
tar -h -C "$ARCHIVE_DIR" --exclude=./build --exclude=./.git -cf /dev/null .

# Check the archived ignore rules against a local, machine-specific link.
git -C "$ARCHIVE_DIR" init -q
ln -s "$ARCHIVE_DIR/missing-local-roms" "$ARCHIVE_DIR/roms"
git -C "$ARCHIVE_DIR" check-ignore -q roms
echo 'PASS: source archive streams without ROMs; local ROM symlink is ignored'
