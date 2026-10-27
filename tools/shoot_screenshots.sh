#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Captures the store screenshots of every platform into assets/screenshots/, from
# the demo build of the watchapp: made-up contacts and messages, no phone needed.
# Five per platform, the store maximum: three animated GIFs and two still PNGs.
#
# The demo data is dated from DEMO_NOW, the emulator runs in UTC and its clock is
# set to DEMO_NOW before each scene, so every run shows the same content. Stills
# are identical from one run to the next; GIF frame timing may vary slightly. The
# emulator resynchronises its clock when an app starts, and a status bar only
# picks up a new time when its window opens, so the clock is set once the app is
# up and before the scene's windows open.
#
# The SDK emulators run firmware 4.33, older than the colour emoji fonts of 4.39.
# When PEBBLEOS points to a PebbleOS checkout holding build-qemu-<platform>/, that
# platform runs this firmware build instead, in the QEMU of the PebbleOS CI image
# (tools/qemu.Dockerfile). PebbleOS has QEMU boards for emery, gabbro and flint
# only. In the PebbleOS CI image, for each platform:
#   pbl -b build-qemu-emery configure --board qemu_emery
#   pbl -b build-qemu-emery build qemu_image_micro qemu_image_spi
#
# The demo build replaces the normal one in build/: the script cleans it on
# the way out, so it can never be installed or published by mistake.
#
# Needs ffmpeg and uv; Docker for firmware builds. An SDK platform needs the
# emulators to itself: `pebble kill` stops every one of them.
#
# Usage: PEBBLEOS=~/src/PebbleOS tools/shoot_screenshots.sh [platform...]

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/assets/screenshots"
# Tuesday 20 October 2026, 14:32 UTC.
DEMO_NOW=1792506720
PLATFORMS="${*:-basalt chalk diorite emery flint gabbro}"
IMAGE=pebble-phone-qemu
CONTAINER=pebble-phone-qemu
QEMU_PORT=12344
MONITOR_PORT=47444

export TZ=UTC
export PEBBLE_PHONE_DEMO="$DEMO_NOW"

firmware() { [ -n "${PEBBLEOS:-}" ] && [ -f "$PEBBLEOS/build-qemu-$1/pebbleos.elf" ]; }

used_sdk=false
cleanup() {
  docker rm -f "$CONTAINER" >/dev/null 2>&1 || true
  if $used_sdk; then
    pebble kill >/dev/null 2>&1 || true
  fi
  pebble clean >/dev/null 2>&1 || true
  rm -rf "$ROOT/build/qemu"
}

cd "$ROOT"
trap cleanup EXIT

pebble clean >/dev/null
pebble build >/dev/null
mkdir -p "$OUT"
for platform in $PLATFORMS; do
  rm -f "$OUT/${platform}"_*
done

# Boots the firmware build with a fresh copy of its flash, then installs the app.
boot_firmware() {
  local build="$PEBBLEOS/build-qemu-$1" work="$ROOT/build/qemu"
  # Only the emery and flint machines have a speaker.
  local machine=(-machine "pebble-$1")
  case "$1" in
    emery | flint) machine=(-machine "pebble-$1,audiodev=snd0" -audiodev none,id=snd0) ;;
  esac
  docker build -q -t "$IMAGE" -f "$ROOT/tools/qemu.Dockerfile" "$ROOT/tools" >/dev/null
  docker rm -f "$CONTAINER" >/dev/null 2>&1 || true
  mkdir -p "$work"
  cp "$build/qemu_spi_flash.bin" "$work/spi_flash.bin"
  # The monitor writes the GIF frames itself, hence build/ at its own path.
  docker run -d --rm --name "$CONTAINER" -e TZ=UTC \
    -p "127.0.0.1:$QEMU_PORT:$QEMU_PORT" -p "127.0.0.1:$MONITOR_PORT:4444" \
    -v "$build:$build:ro" -v "$ROOT/build:$ROOT/build" \
    "$IMAGE" /opt/pebbleos-sdk/qemu/bin/qemu-pebble \
    -rtc base=utc -display none \
    -monitor tcp:0.0.0.0:4444,server=on,wait=off \
    -serial null -serial "tcp:0.0.0.0:$QEMU_PORT,server=on,wait=off" -serial null \
    "${machine[@]}" \
    -kernel "$build/pebbleos.elf" -drive "if=mtd,format=raw,file=$work/spi_flash.bin" >/dev/null
  sleep 25
  pebble install --qemu "localhost:$QEMU_PORT" build/pebble-phone-sms.pbw >/dev/null
}

for platform in $PLATFORMS; do
  echo "== $platform"
  if firmware "$platform"; then
    target=(--qemu "localhost:$QEMU_PORT")
    export QEMU_MONITOR_PORT=$MONITOR_PORT QEMU_PEBBLE_PORT=$QEMU_PORT
  else
    target=(--emulator "$platform")
    unset QEMU_MONITOR_PORT QEMU_PEBBLE_PORT
  fi

  clock() { pebble emu-set-time "${target[@]}" "$DEMO_NOW" >/dev/null; }
  # Buttons only, or a recorded GIF: see tools/record_gif.py for the steps.
  keys() { uv run "$ROOT/tools/record_gif.py" "$platform" - "$1" >/dev/null; }
  gif() {
    uv run "$ROOT/tools/record_gif.py" "$platform" "$OUT/${platform}_$1.gif" "$2" >/dev/null
    echo "   $1.gif"
  }
  png() {
    pebble screenshot "${target[@]}" --no-open "$OUT/${platform}_$1.png" >/dev/null 2>&1
    echo "   $1.png"
  }

  if firmware "$platform"; then
    echo "   firmware $(git -C "$PEBBLEOS" describe --tags --dirty)"
    boot_firmware "$platform"
  else
    used_sdk=true
    pebble kill >/dev/null 2>&1 || true
    pebble install "${target[@]}" >/dev/null
  fi
  sleep 6
  # The firmware's default differs between builds.
  pebble emu-time-format "${target[@]}" --format 12h >/dev/null
  clock

  # From the menu: the favorites, browsed card by card.
  gif 1_favorites "rec wait:0.6 select wait:1.8 down wait:1.6 down wait:1.6 up wait:1.3 up wait:1.5 stop"

  # Select on a card lists its actions, Call first. Down hangs up and the call
  # screen closes itself, back on the actions; two backs reach the menu.
  clock
  keys "select wait:1.5 select wait:2"
  png 2_call
  keys "down wait:4 back wait:1 back wait:1"

  # Messages, the fourth row of the menu.
  clock
  keys "down wait:0.4 down wait:0.4 down wait:0.4 select wait:2"
  png 5_conversations

  # The first conversation; its first row answers it, the first message is under
  # it. Down peeks at the next message, then brings it in.
  clock
  gif 3_messages "rec wait:0.6 select wait:1.8 down wait:0.6 select wait:2 down wait:1.2 down wait:1.6 down wait:1.2 down wait:1.8 stop"

  # Select answers: the emoji grid, then the system send animation.
  clock
  gif 4_reply "rec wait:0.6 select wait:1.5 down wait:0.8 select wait:1.4 down wait:0.4 down wait:0.4 down wait:0.4 down wait:0.4 down wait:0.8 select wait:4.5 stop"

  if firmware "$platform"; then
    docker rm -f "$CONTAINER" >/dev/null
  fi
done

echo "Done: $OUT"
