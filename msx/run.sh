#!/bin/bash
# ZX Desk for MSX: build the ROM and run it in a visible openMSX.
#   msx/run.sh                 C-BIOS_MSX1_EU, mouse in port A
#   MSX_MACHINE=C-BIOS_MSX1_JP msx/run.sh
#   MSX_MACHINE=Philips_VG_8020 msx/run.sh   a real machine, ROMs in msx/roms/
#
# Uses the host's openmsx when installed (Fedora: dnf install openmsx
# cbios), otherwise the toolchain image with the X socket passed in
# (works under Wayland through XWayland). Cursor keys move the pointer,
# SPACE is the button, the host mouse is the MSX mouse once the window
# has focus; F12 opens the openMSX console.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MACHINE="${MSX_MACHINE:-C-BIOS_MSX1_EU}"
IMAGE=zxdesk-msx

docker image inspect "$IMAGE" >/dev/null 2>&1 || docker build -t "$IMAGE" "$ROOT/msx/docker"
docker run --rm -v "$ROOT:/work" -w /work/msx -u "$(id -u):$(id -g)" "$IMAGE" \
  pasmo -I src --bin src/msxdesk.asm build/msxdesk.rom build/msxdesk.sym 2>&1 | grep -v "WARNING: Var\|3 pass" || true
[[ -s "$ROOT/msx/build/msxdesk.rom" ]] || { echo "no ROM built" >&2; exit 1; }

ARGS=(-machine "$MACHINE" -cart "$ROOT/msx/build/msxdesk.rom" -romtype page12 -command "plug joyporta mouse")
if command -v openmsx >/dev/null; then
  # Fedora's cbios package is not linked into openMSX's system ROMs;
  # the user share dir is searched too, so link them there once.
  USERROMS="$HOME/.openMSX/share/systemroms"
  mkdir -p "$USERROMS"
  if [[ -d /usr/share/cbios && ! -e "$USERROMS/cbios_main_msx1_eu.rom" ]]; then
    ln -sf /usr/share/cbios/*.rom "$USERROMS/"
  fi
  # real BIOS ROMs from msx/roms (gitignored), matched by sha1
  if [[ -d "$ROOT/msx/roms" ]]; then
    ln -sf "$ROOT"/msx/roms/* "$USERROMS/"
  fi
  exec openmsx "${ARGS[@]}"
fi
# Fallback: the image on the host display. Needs the GPU passed in for
# openMSX's GL renderer; without it the window opens and hangs.
xhost +local: >/dev/null 2>&1 || true
exec docker run --rm -it -e DISPLAY="${DISPLAY:-:0}" -e HOME=/tmp -e SDL_AUDIODRIVER=dummy \
  --device /dev/dri -v /tmp/.X11-unix:/tmp/.X11-unix -v "$ROOT:$ROOT:ro" \
  -u "$(id -u):$(id -g)" -v /etc/passwd:/etc/passwd:ro "$IMAGE" \
  sh -c 'mkdir -p /tmp/.openMSX/share && ln -sfn "$0" /tmp/.openMSX/share/systemroms; exec openmsx "$@"' "$ROOT/msx/roms" "${ARGS[@]}"
