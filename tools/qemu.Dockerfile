# The PebbleOS CI image plus the SDL runtime its qemu-pebble links against,
# even when run headless.
FROM ghcr.io/coredevices/pebbleos-docker:v7
RUN apt-get update \
 && apt-get install -y --no-install-recommends libsdl2-2.0-0 \
 && rm -rf /var/lib/apt/lists/*
