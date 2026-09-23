#!/bin/bash
# ZX Desk for MSX: build the toolchain image once, then run msxtest.py in it.
#   msx/test.sh            build + assert
#   msx/test.sh --shell    a shell in the image instead
#   REBUILD=1 msx/test.sh  rebuild the image after a Dockerfile change
#
# The tree is streamed in with tar and msx/build streamed back out rather
# than bind mounted, so this also works from inside a container that only
# has the docker socket (the LogicForce worker): a bind mount there would
# name a path the daemon cannot see. Test output goes to stderr because
# stdout carries the tar stream. -h follows symlinks, so msx/roms may be
# a link to wherever the BIOS ROMs live on the machine running this.
set -eo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE=zxdesk-msx
if ! docker image inspect "$IMAGE" >/dev/null 2>&1 || [[ -n "${REBUILD:-}" ]]; then
  docker build -t "$IMAGE" "$ROOT/msx/docker"
fi
if [[ "${1:-}" == "--shell" ]]; then
  exec docker run --rm -it -v "$ROOT:/work" -w /work/msx -e HOME=/tmp -u "$(id -u):$(id -g)" -v /etc/passwd:/etc/passwd:ro "$IMAGE" bash
fi
tar -h -C "$ROOT" --exclude=./msx/build --exclude=./.git -cf - . \
  | docker run --rm -i -e "MSX_MACHINE=${MSX_MACHINE:-C-BIOS_MSX1_EU}" -e "MSX_EXT=${MSX_EXT:-}" "$IMAGE" sh -c \
      'mkdir -p /work && cd /work && tar xf - && cd msx && python3 msxtest.py "$@" >&2; rc=$?; tar -cf - build; exit $rc' \
      -- "$@" \
  | tar -C "$ROOT/msx" -xf -
