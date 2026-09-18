#!/bin/zsh
# ZX Desk: run a .tap in Fuse and record the emulator window to video.
#   ./record.sh build/zxdesk-script-tourdemo.tap 20
# The same window-bounds dance as run.sh, and for the same reason:
# screencapture takes a screen rectangle rather than a window, so Fuse
# has to be frontmost and its bounds read immediately before the shot.
set -e
ROOT="${0:A:h}"
TAP="${1:-$ROOT/build/zxdesk-script-tourdemo.tap}"
SECS="${2:-20}"
SETTLE="${3:-3}"

osascript -e 'tell application "Fuse" to quit' 2>/dev/null || true
for i in {1..20}; do
  pgrep -f "/Applications/Fuse.app/Contents/MacOS/Fuse" >/dev/null || break
  sleep 0.5
done

caffeinate -u -t 1
caffeinate -di -t $((SECS + 90)) &
CAFF=$!
trap 'kill $CAFF 2>/dev/null' EXIT

open -a Fuse "$TAP"
BOUNDS=""
for i in {1..30}; do
  BOUNDS=$(osascript -e 'tell application "System Events" to tell process "Fuse" to get {position, size} of window 1' 2>/dev/null) && break
  sleep 1
done
[[ -z "$BOUNDS" ]] && { echo "Fuse came up with no window." >&2; exit 1; }

osascript -e 'tell application "Fuse" to activate' 2>/dev/null || true
sleep "$SETTLE"
BOUNDS=$(osascript -e 'tell application "System Events" to tell process "Fuse" to get {position, size} of window 1' 2>/dev/null)
FRONT=$(osascript -e 'tell application "System Events" to get name of first process whose frontmost is true' 2>/dev/null)
[[ "$FRONT" != "Fuse" ]] && { echo "Fuse is not frontmost (front is $FRONT)." >&2; exit 1; }

OUT="$ROOT/shots/$(date +%H%M%S)-$(basename "${TAP%.tap}").mov"
screencapture -v -V "$SECS" -R "$(echo "$BOUNDS" | tr -d ' ')" "$OUT"
echo "$OUT"
