#!/bin/zsh
# ZX Desk: drive the esxDOS machine under Fuse.
#   ./esxdos.sh flash              rebuild the esxDOS machine from cold
#   ./esxdos.sh put build/x.tap    copy a tap onto the card as ESXTEST.TAP
#   ./esxdos.sh browse             open the NMI file browser
#   ./esxdos.sh run                browse, pick ESXTEST.TAP, load it
#   ./esxdos.sh shot out.png       write the emulated screen to a PNG
#
# The order that matters is: put, then flash. Fuse holds the card image
# open while it runs and esxDOS caches the directory, so a file added to
# the card after boot is not there as far as esxDOS is concerned.
#
# Keys have to be held, not tapped. Fuse samples the emulated keyboard
# matrix once a frame, and System Events' `key code` posts the down and
# the up faster than that, so most presses fall between two polls and
# vanish. Forty taps once moved the cursor four rows. `key down`, a
# pause of 80ms, `key up` registers every single time: three presses
# move three rows, every time. That is what `key` below does, and it is
# why this script can drive the browser at all.
#
# Why there is no snapshot here. There was one, and it cost a day. A
# .szx restores the directory esxDOS had cached when it was saved, so a
# file added afterwards never appears, a soft reset does not clear it,
# and swapping the card image underneath it does not either. Worse, the
# results measured against it are not measurements. Flashing from cold
# takes twenty seconds and is always right. docs/esxdos.md has the rest.
set -e
ROOT="${0:A:h}"
HDF="$ROOT/tools/esxdos/esxdos.hdf"
FLASHER="$ROOT/tools/esxdos/ESXMMC.TAP"
FUSEDOM=net.sourceforge.fuse-for-macosx.Fuse
HDFMONKEY="$ROOT/tools/bin/hdfmonkey"

quit_fuse() {
  osascript -e 'tell application "Fuse" to quit' 2>/dev/null || true
  for i in {1..20}; do
    pgrep -f "/Applications/Fuse.app/Contents/MacOS/Fuse" >/dev/null || break
    sleep 0.5
  done
}

# Fuse has to be frontmost before its menus will take a synthetic click,
# and activate does not always win first time.
front() {
  for i in {1..3}; do
    osascript -e 'tell application "Fuse" to activate' 2>/dev/null || true
    sleep 1
    F=$(osascript -e 'tell application "System Events" to get name of first process whose frontmost is true' 2>/dev/null)
    [[ "$F" == "Fuse" ]] && return 0
  done
  echo "Fuse would not come to the front (front is $F)." >&2
  return 1
}

# key <keycode> [<hold seconds>]. Held across four frames, because a
# tapped key is not seen. 125 is down, 36 is enter, 49 is space.
key() {
  front
  osascript -e "tell application \"System Events\" to tell process \"Fuse\"
    key down ${1}
    delay ${2:-0.08}
    key up ${1}
  end tell" 2>/dev/null
  sleep 0.25
}

menu() {  # menu <top> <item>, or menu <top> <sub> <item>
  front
  if [[ -n "${3:-}" ]]; then
    osascript -e "tell application \"System Events\" to tell process \"Fuse\" to click menu item \"$3\" of menu 1 of menu item \"$2\" of menu 1 of menu bar item \"$1\" of menu bar 1"
  else
    osascript -e "tell application \"System Events\" to tell process \"Fuse\" to click menu item \"$2\" of menu 1 of menu bar item \"$1\" of menu bar 1"
  fi
}

case "${1:-}" in

put)
  TAP="${2:?usage: esxdos.sh put <tap>}"
  quit_fuse                       # hdfmonkey must not write under Fuse
  "$HDFMONKEY" rm "$HDF" /ESXTEST.TAP 2>/dev/null || true
  "$HDFMONKEY" put "$HDF" "$TAP" /ESXTEST.TAP
  "$HDFMONKEY" ls "$HDF" / | tail -1
  ;;

flash)
  # Write protect off is the emulated equivalent of the JP2/E jumper the
  # flasher asks for. The reset afterwards must be a soft one: a hard
  # reset throws the flashed EEPROM away, which is what made the first
  # attempt at this look like a failure when it had worked.
  quit_fuse
  defaults write $FUSEDOM divmmc -int 1
  defaults write $FUSEDOM divmmcfile -string "$HDF"
  defaults write $FUSEDOM divmmcwriteprotect -int 0
  open -a Fuse "$FLASHER"
  sleep 14
  key 49                          # answer the flasher's HIT KEY prompt
  sleep 8
  menu Machine Reset
  sleep 6
  echo "flashed. ./esxdos.sh browse to check, then pick ESXTEST.TAP"
  ;;

browse)
  menu Machine NMI
  ;;

run)
  # ESXTEST.TAP is put last, so walking to the bottom lands on it
  # whatever else is there: the cursor stops at the end rather than
  # wrapping, so overshooting costs nothing.
  menu Machine NMI
  sleep 3
  for i in {1..16}; do key 125; done
  key 36
  sleep 14
  echo "loaded from the card"
  ;;

shot)
  OUT="${2:?usage: esxdos.sh shot <out.png>}"
  # Fuse writing its own framebuffer, rather than screencapture taking a
  # screen rectangle. No screen recording permission needed, and nothing
  # sitting on top of the window can get into the picture.
  rm -f "$OUT"
  front
  # A save dialog left open by a previous failed shot keeps focus and
  # swallows everything sent to the emulator afterwards, which looks
  # exactly like keys being dropped.
  osascript -e 'tell application "System Events" to tell process "Fuse"
    if exists window "Export Screenshot" then keystroke (ASCII character 27)
  end tell' 2>/dev/null
  sleep 1
  osascript <<EOF
tell application "System Events"
  tell process "Fuse"
    click menu item "Export Screenshot…" of menu 1 of menu bar item "File" of menu bar 1
    delay 2
    keystroke "g" using {command down, shift down}
    delay 1
    keystroke "$OUT"
    delay 1
    key code 36
    delay 1
    key code 36
  end tell
end tell
EOF
  sleep 2
  [[ -f "$OUT" ]] && echo "$OUT" || { echo "no screenshot written" >&2; exit 1; }
  ;;

*)
  sed -n '2,6p' "$0"
  exit 1
  ;;
esac
