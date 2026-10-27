#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Records an animated GIF of a running emulator while pressing its buttons.

Frames come from the QEMU monitor (screendump), as `pebble screenshot` does. Each
frame is brought back to full brightness, since the display dims with the
backlight, then snapped to the 64 colours of the watch.

Buttons go through the QEMU protocol, as `pebble emu-button` does but over one
connection kept open: a press, then a release PRESS_S later. The monitor's
sendkey cannot stand in for it, the firmware may see its key held past the 400 ms
that start a repeat or a long click.

Steps, separated by spaces:
  up, down, select, back    press a button
  wait:1.5                  pause for that many seconds
  rec / stop                start or stop recording

Usage: uv run tools/record_gif.py PLATFORM OUT.gif "select wait:1 rec down wait:1 stop"
With - for OUT.gif, the steps only press buttons.
Needs ffmpeg and an emulator started by `pebble install --emulator PLATFORM`, or
any QEMU whose monitor listens on the TCP port in QEMU_MONITOR_PORT and whose
Pebble serial port is QEMU_PEBBLE_PORT. The monitor writes the frames itself, so
that QEMU must see build/ at the same path.
"""

import base64
import json
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

FPS = 15
# State bits of QemuProtocol_Button.
BUTTONS = {"back": 1, "up": 2, "select": 4, "down": 8}
QEMU_BUTTON = 8
PRESS_S = 0.1
# The pypkjs websocket opcode that relays a QEMU protocol message.
RELAY_QEMU = 0x0B
WORK = Path(__file__).resolve().parent.parent / "build" / "gif"


def emulator(platform: str) -> dict:
    info = json.loads((Path(tempfile.gettempdir()) / "pb-emulator.json").read_text())
    for sdk in info.get(platform, {}).values():
        return sdk
    raise SystemExit("No emulator running for " + platform)


def monitor_port(platform: str) -> int:
    # A QEMU started outside pebble-tool, such as a firmware build's.
    if os.environ.get("QEMU_MONITOR_PORT"):
        return int(os.environ["QEMU_MONITOR_PORT"])
    return emulator(platform)["qemu"]["monitor"]


class Buttons:
    """Straight to the QEMU serial port, or relayed by the emulator's pypkjs."""

    def __init__(self, platform: str):
        if os.environ.get("QEMU_PEBBLE_PORT"):
            self.sock = socket.create_connection(("127.0.0.1", int(os.environ["QEMU_PEBBLE_PORT"])))
            self.websocket = False
            return
        port = emulator(platform)["pypkjs"]["port"]
        self.sock = socket.create_connection(("127.0.0.1", port))
        self.websocket = True
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall(
            "GET / HTTP/1.1\r\nHost: localhost:{}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
            "Sec-WebSocket-Key: {}\r\nSec-WebSocket-Version: 13\r\n\r\n".format(port, key).encode()
        )
        response = b""
        while b"\r\n\r\n" not in response:
            chunk = self.sock.recv(1024)
            if not chunk:
                break
            response += chunk
        if b" 101 " not in response.split(b"\r\n", 1)[0]:
            raise SystemExit("pypkjs refused the websocket")

    def send(self, state: int) -> None:
        if not self.websocket:
            self.sock.sendall(struct.pack(">HHHBH", 0xFEED, QEMU_BUTTON, 1, state, 0xBEEF))
            return
        # A client frame is always masked; three bytes need no extended length.
        payload = bytes([RELAY_QEMU, QEMU_BUTTON, state])
        mask = os.urandom(4)
        masked = bytes(byte ^ mask[i % 4] for i, byte in enumerate(payload))
        self.sock.sendall(bytes([0x82, 0x80 | len(payload)]) + mask + masked)

    def press(self, name: str) -> None:
        self.send(BUTTONS[name])
        time.sleep(PRESS_S)
        self.send(0)

    def close(self) -> None:
        self.sock.close()


def command(port: int, line: str) -> None:
    with socket.create_connection(("127.0.0.1", port), timeout=1) as sock:
        sock.settimeout(0.2)
        try:
            sock.recv(4096)
        except OSError:
            pass
        sock.sendall((line + "\n").encode())
        try:
            sock.recv(4096)
        except OSError:
            pass


def snap(path: Path) -> None:
    """Undoes the backlight dimming and snaps every channel to 0, 85, 170 or 255."""
    data = path.read_bytes()
    # P6 header: magic, width, height, maximum, each followed by one whitespace.
    fields, end = [], 0
    while len(fields) < 4:
        start = end
        while data[end] not in b" \t\n\r":
            end += 1
        fields.append(data[start:end])
        end += 1
    header, pixels = data[:end], data[end:]
    peak = max(pixels) or 255
    gain = 255 / peak if peak > 64 else 1
    table = bytes(min(3, round(v * gain / 85)) * 85 for v in range(256))
    path.write_bytes(header + pixels.translate(table))


class Recorder(threading.Thread):
    def __init__(self, port: int, directory: Path):
        super().__init__(daemon=True)
        self.port = port
        self.directory = directory
        self.recording = threading.Event()
        self.done = threading.Event()
        self.frames: list[Path] = []

    def run(self) -> None:
        interval = 1 / FPS
        next_frame = time.perf_counter()
        while not self.done.is_set():
            if self.recording.is_set():
                path = self.directory / "frame_{:05d}.ppm".format(len(self.frames))
                command(self.port, "screendump {}".format(path))
                deadline = time.time() + 0.5
                while time.time() < deadline and not (path.exists() and path.stat().st_size > 0):
                    time.sleep(0.005)
                if path.exists():
                    self.frames.append(path)
            next_frame += interval
            time.sleep(max(0, next_frame - time.perf_counter()))


def main() -> None:
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    platform, output, steps = sys.argv[1], Path(sys.argv[2]), sys.argv[3].split()
    port = monitor_port(platform)
    directory = WORK / platform
    shutil.rmtree(directory, ignore_errors=True)
    directory.mkdir(parents=True)

    buttons = Buttons(platform)
    recorder = Recorder(port, directory)
    recorder.start()
    for step in steps:
        name, *args = step.split(":")
        if name == "rec":
            recorder.recording.set()
        elif name == "stop":
            recorder.recording.clear()
        elif name == "wait":
            time.sleep(float(args[0]))
        else:
            buttons.press(name)
    buttons.close()
    recorder.done.set()
    recorder.join()
    if str(output) == "-":
        shutil.rmtree(directory, ignore_errors=True)
        return

    # Frames written while the recording stopped are dropped.
    frames = [frame for frame in recorder.frames if frame.exists()]
    if not frames:
        raise SystemExit("No frame recorded")
    for index, frame in enumerate(frames):
        snap(frame)
        frame.rename(directory / "seq_{:05d}.ppm".format(index))
    sequence = str(directory / "seq_%05d.ppm")
    palette = directory / "palette.png"
    subprocess.run(
        ["ffmpeg", "-v", "error", "-y", "-framerate", str(FPS), "-i", sequence,
         "-vf", "palettegen=max_colors=64:reserve_transparent=0", str(palette)],
        check=True,
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        ["ffmpeg", "-v", "error", "-y", "-framerate", str(FPS), "-i", sequence, "-i", str(palette),
         "-filter_complex", "paletteuse=dither=none", str(output)],
        check=True,
    )
    shutil.rmtree(directory, ignore_errors=True)
    print("{}: {} frames".format(output.name, len(frames)))


if __name__ == "__main__":
    main()
