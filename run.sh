#!/bin/bash
# ZX Desk for MSX: build the ROM and run it in a visible openMSX.
#   ./run.sh                 C-BIOS_MSX1_EU, mouse in port A
#   MSX_MACHINE=C-BIOS_MSX1_JP ./run.sh
#   MSX_MACHINE=Roms_MSX1 ./run.sh    the real BIOS in roms/ (Roms_MSX2 too)
#   MSX_MACHINE=Roms_MSX1 MSX_EXT=Roms_Disk MSX_DISK=build/disk.dsk ./run.sh
#
# Uses the host's openmsx when installed (Fedora: dnf install openmsx
# cbios), otherwise the toolchain image with the X socket passed in
# (works under Wayland through XWayland). Cursor keys move the pointer,
# CTRL is the button, the host mouse is the MSX mouse once the window
# has focus and is grabbed (openMSX gets relative motion only while the
# pointer is inside the window, so it is grabbed; F12 opens the console,
# `set grabinput off` lets go).
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
MACHINE="${MSX_MACHINE:-C-BIOS_MSX1_EU}"
IMAGE=zxdesk-msx

docker image inspect "$IMAGE" >/dev/null 2>&1 || docker build -t "$IMAGE" "$ROOT/docker"
docker run --rm -v "$ROOT:/work" -w /work -u "$(id -u):$(id -g)" "$IMAGE" \
  pasmo -I src --bin src/msxdesk.asm build/msxdesk.rom build/msxdesk.sym 2>&1 | grep -v "WARNING: Var\|3 pass" || true
[[ -s "$ROOT/build/msxdesk.rom" ]] || { echo "no ROM built" >&2; exit 1; }

ARGS=(-machine "$MACHINE" -cartb "$ROOT/build/msxdesk.rom" -romtype page12 -command "plug joyporta mouse" -command "set grabinput on")
# MSX_EXT=Roms_Disk puts the disk interface in slot 1, ahead of the
# cartridge in slot 2, so its init runs first; MSX_DISK=file.dsk mounts an
# image (the harness's build/disk.dsk is a blank 720K one).
[[ -n "${MSX_EXT:-}" ]] && ARGS+=(-ext "$MSX_EXT")
[[ -n "${MSX_DISK:-}" ]] && ARGS+=(-diska "$MSX_DISK")
if command -v openmsx >/dev/null; then
  # Fedora's cbios package is not linked into openMSX's system ROMs;
  # the user share dir is searched too, so link them there once.
  USERROMS="$HOME/.openMSX/share/systemroms"
  mkdir -p "$USERROMS"
  if [[ -d /usr/share/cbios && ! -e "$USERROMS/cbios_main_msx1_eu.rom" ]]; then
    ln -sf /usr/share/cbios/*.rom "$USERROMS/"
  fi
  # real BIOS ROMs from roms (gitignored), matched by sha1, and the
  # machine configs built on them
  if [[ -d "$ROOT/roms" ]]; then
    ln -sf "$ROOT"/roms/* "$USERROMS/"
  fi
  mkdir -p "$HOME/.openMSX/share/machines" "$HOME/.openMSX/share/extensions"
  ln -sf "$ROOT"/harness/machines/*.xml "$HOME/.openMSX/share/machines/"
  ln -sf "$ROOT"/harness/extensions/*.xml "$HOME/.openMSX/share/extensions/"
  exec openmsx "${ARGS[@]}"
fi
# Fallback: the image on the host display. Needs the GPU passed in for
# openMSX's GL renderer; without it the window opens and hangs.
xhost +local: >/dev/null 2>&1 || true
exec docker run --rm -it -e DISPLAY="${DISPLAY:-:0}" -e HOME=/tmp -e SDL_AUDIODRIVER=dummy \
  --device /dev/dri -v /tmp/.X11-unix:/tmp/.X11-unix -v "$ROOT:$ROOT:ro" \
  -u "$(id -u):$(id -g)" -v /etc/passwd:/etc/passwd:ro "$IMAGE" \
  sh -c 'mkdir -p /tmp/.openMSX/share && ln -sfn "$0" /tmp/.openMSX/share/systemroms && ln -sfn "$1" /tmp/.openMSX/share/machines && ln -sfn "$2" /tmp/.openMSX/share/extensions; shift 2; exec openmsx "$@"' "$ROOT/roms" "$ROOT/harness/machines" "$ROOT/harness/extensions" "${ARGS[@]}"
