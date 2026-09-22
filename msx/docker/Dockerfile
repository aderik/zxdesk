# MSX1 port of ZX Desk: build + headless test image.
#
# openMSX 20.0 (Debian trixie; 19.1 is not packaged anywhere current),
# C-BIOS 0.29a, pasmo 0.5.5 built from source (Debian's 0.5.3 has no
# IFDEF, which the ZX source uses on line 88 and everywhere else),
# Xvfb because openMSX's SDL video init asserts under SDL_VIDEODRIVER=dummy,
# xdotool to drive the emulated mouse through the Xvfb pointer.
FROM debian:trixie-slim
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        openmsx cbios xvfb xdotool x11-utils python3 ca-certificates curl g++ make \
    && rm -rf /var/lib/apt/lists/*

# The Debian cbios package only links a subset of its ROMs into openMSX's
# systemroms, and C-BIOS_MSX1_EU (the 50 Hz machine) is not among them.
RUN ln -sf /usr/share/cbios/*.rom /usr/share/openmsx/systemroms/

ARG PASMO_SHA256=c83ff23e06b26ab5de05efaf13d9ebaf485d43f9a3a1ed50bba17be5a87918ac
RUN cd /tmp && curl -sSL -o pasmo.tar.gz https://pasmo.speccy.org/bin/pasmo-0.5.5.tar.gz \
    && echo "$PASMO_SHA256  pasmo.tar.gz" | sha256sum -c - \
    && tar xzf pasmo.tar.gz && cd pasmo-0.5.5 \
    && ./configure >/dev/null && make -j >/dev/null && make install >/dev/null \
    && cd / && rm -rf /tmp/pasmo*

ENV DISPLAY=:99 SDL_AUDIODRIVER=dummy
WORKDIR /work
