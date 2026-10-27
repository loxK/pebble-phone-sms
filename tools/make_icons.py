#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Converts the Pebble icons of assets/icons/ to PDC images for the watchapp.

The icons come from the PebbleOS iconography (Apache-2.0). They use a small part
of SVG: absolute M, L, H, V and Z paths, polygons, polylines and lines, black
strokes and white fills. Each subpath becomes its own command, which the
PebbleOS converter does not do. Points are encoded as PebbleOS does: moved by
half a pixel, then in eighths of a pixel (precise paths).

launcher.svg becomes the launcher icon instead: a 25 px PNG, black on clear, which
the launcher tints as it does its own icons. The glance icons, shown in the
launcher next to the latest event, are made the same way: the SDK cannot measure
a PDC image to publish it. Those PNGs need rsvg-convert and ImageMagick.

Usage: uv run tools/make_icons.py
"""

import math
import re
import struct
import subprocess
import xml.etree.ElementTree as ElementTree
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCES = ROOT / "assets" / "icons"
OUTPUT = ROOT / "resources" / "pdc"
IMAGES = ROOT / "resources" / "images"
LAUNCHER = IMAGES / "menu_icon.png"
GLANCE = {
    SOURCES / "missed.svg": IMAGES / "glance_missed.png",
    ROOT / "assets" / "glance" / "sms.svg": IMAGES / "glance_sms.png",
}

PRECISE_PATH = 3
COLORS = {"black": 0xC0, "#000000": 0xC0, "white": 0xFF, "#ffffff": 0xFF, "none": 0x00}


def precise(value: float) -> int:
    return math.floor((value - 0.5) * 8 + 0.5)


def color(element: ElementTree.Element, root: ElementTree.Element, name: str) -> int:
    value = element.get(name, root.get(name, "none")).lower()
    if value not in COLORS:
        raise SystemExit("Unsupported {} {}".format(name, value))
    return COLORS[value]


def numbers(text: str) -> list[float]:
    return [float(n) for n in re.findall(r"-?\d*\.?\d+", text)]


def subpaths(d: str) -> list[tuple[list[tuple[float, float]], bool]]:
    """Splits a path into its subpaths: points, and whether Z closes it."""
    result, points, closed = [], [], False
    x = y = 0.0
    for command, arguments in re.findall(r"([MLHVZmlhvzCcSsQqTtAa])([^MLHVZmlhvzCcSsQqTtAa]*)", d):
        values = numbers(arguments)
        if command == "M":
            if points:
                result.append((points, closed))
            points, closed = [], False
            pairs = list(zip(values[0::2], values[1::2]))
            for x, y in pairs:
                points.append((x, y))
        elif command == "L":
            for x, y in zip(values[0::2], values[1::2]):
                points.append((x, y))
        elif command == "H":
            for x in values:
                points.append((x, y))
        elif command == "V":
            for y in values:
                points.append((x, y))
        elif command == "Z":
            closed = True
            x, y = points[0]
        else:
            raise SystemExit("Unsupported path command " + command)
    if points:
        result.append((points, closed))
    return result


def shapes(element: ElementTree.Element) -> list[tuple[list[tuple[float, float]], bool]]:
    tag = element.tag.split("}")[-1]
    if tag == "path":
        return subpaths(element.get("d"))
    if tag in ("polygon", "polyline"):
        values = numbers(element.get("points"))
        return [(list(zip(values[0::2], values[1::2])), tag == "polygon")]
    if tag == "line":
        return [([(float(element.get("x1")), float(element.get("y1"))),
                  (float(element.get("x2")), float(element.get("y2")))], False)]
    return []


def convert(svg: Path) -> bytes:
    root = ElementTree.parse(svg).getroot()
    box = numbers(root.get("viewBox"))
    width, height = round(box[2]), round(box[3])
    commands = []
    for element in root.iter():
        stroke = color(element, root, "stroke")
        stroke_width = round(float(element.get("stroke-width", "0"))) if stroke else 0
        fill = color(element, root, "fill")
        for points, closed in shapes(element):
            # A filled area keeps its outline closed even without Z.
            closed = closed or (fill and not stroke)
            command = struct.pack("<BBBBBBBH", PRECISE_PATH, 0, stroke, stroke_width, fill,
                                  0 if closed else 1, 0, len(points))
            command += b"".join(struct.pack("<hh", precise(x), precise(y)) for x, y in points)
            commands.append(command)
    image = struct.pack("<BBhhH", 1, 0, width, height, len(commands)) + b"".join(commands)
    return b"PDCI" + struct.pack("<I", len(image)) + image


def png_icon(svg: Path, target: Path) -> None:
    png = subprocess.run(["rsvg-convert", "-w", "25", "-h", "25", str(svg)], check=True,
                         capture_output=True).stdout
    # Hard edges, as the system's 1-bit icons have.
    subprocess.run(["convert", "png:-", "-background", "white", "-alpha", "remove", "-alpha", "off",
                    "-colorspace", "Gray", "-threshold", "50%", "-transparent", "white",
                    "-define", "png:color-type=6", str(target)], input=png, check=True)
    print("Wrote", target.relative_to(ROOT))


def main() -> None:
    for svg in sorted(SOURCES.glob("*.svg")):
        if svg.stem == "launcher":
            png_icon(svg, LAUNCHER)
            continue
        target = OUTPUT / "icon_{}.pdc".format(svg.stem)
        target.write_bytes(convert(svg))
        print("Wrote", target.relative_to(ROOT))
    for svg, target in GLANCE.items():
        png_icon(svg, target)


if __name__ == "__main__":
    main()
