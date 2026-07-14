#!/usr/bin/env python3
# Copyright (c) 2026 The Dash Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

import os
import platform
import shutil
import subprocess
import sys
import tempfile

# Assuming 1024x1024 canvas, the squircle content area is ~864x864 with
# ~80px transparent padding on each side
CONTENT_RATIO = 864 / 1024

DIR_ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", ".."))
DIR_SRC = os.path.join(DIR_ROOT, "src", "qt", "res", "src")
DIR_OUT = os.path.join(DIR_ROOT, "src", "qt", "res", "icons")

# Single vector source for the app icon. Per-network variants are produced by
# swapping the two fills of the artwork:
#   - the square background  (fill="black" in the source)
#   - the foreground logo    (fill="white" in the source)
APP_SVG = os.path.join(DIR_SRC, "pirate.svg")
SRC_BG_FILL = 'fill="black"'
SRC_FG_FILL = 'fill="white"'

# Height (px) the SVG is rasterized to before padding/resampling. Larger than
# any output canvas so downscaling stays crisp.
RENDER_HEIGHT = 1024

# network id -> (runtime icon filename, background color, foreground/logo color)
NETWORKS = [
    ("main",    "piratecash_macos_mainnet.png", "#000000", "#FFFFFF"),
    ("testnet", "piratecash_macos_testnet.png", "#FF6D00", "#0F0018"),
    ("devnet",  "piratecash_macos_devnet.png",  "#7C4DFF", "#0F0018"),
    ("regtest", "piratecash_macos_regtest.png", "#1A0A00", "#FF6600"),
]

# The mainnet colors are also used for the macOS bundle icon (.icns). This name
# must match CFBundleIconFile in share/qt/Info.plist and OSX_INSTALLER_ICONS in
# the Makefile.
ICNS_OUT = "piratecash.icns"

TRAY = os.path.join(DIR_SRC, "tray.svg")

# Canvas to filename mapping for bundle icon
ICNS_MAP = [
    (16, "icon_16x16.png"),
    (32, "icon_16x16@2x.png"),
    (32, "icon_32x32.png"),
    (64, "icon_32x32@2x.png"),
    (128, "icon_128x128.png"),
    (256, "icon_128x128@2x.png"),
    (256, "icon_256x256.png"),
    (512, "icon_256x256@2x.png"),
    (512, "icon_512x512.png"),
    (1024, "icon_512x512@2x.png"),
]

# Maximum height of canvas is 22pt, we use max height instead of recommended
# 16pt canvas to prevent the icon from looking undersized due to icon width.
# See https://bjango.com/articles/designingmenubarextras/
TRAY_MAP = [
    (32, "piratecash_macos_tray.png"),
    (64, "piratecash_macos_tray@2x.png")
]


def sips_resample_padded(src, dst, canvas_size):
    content_size = max(round(canvas_size * CONTENT_RATIO), 1)
    subprocess.check_call(
        ["sips", "-z", str(content_size), str(content_size), "-p", str(canvas_size), str(canvas_size), src, "--out", dst],
        stdout=subprocess.DEVNULL,
    )


def sips_svg_to_png(svg_path, png_path, height):
    subprocess.check_call(
        ["sips", "-s", "format", "png", "--resampleHeight", str(height), svg_path, "--out", png_path],
        stdout=subprocess.DEVNULL,
    )


def recolor_svg(tmpdir, network_id, bg_color, fg_color):
    """Return a temp SVG path with the background/logo fills recolored."""
    with open(APP_SVG, "r", encoding="utf-8") as fp:
        svg = fp.read()
    if SRC_BG_FILL not in svg or SRC_FG_FILL not in svg:
        sys.exit(f"Error: expected fills {SRC_BG_FILL} and {SRC_FG_FILL} not found in {APP_SVG}")
    svg = svg.replace(SRC_BG_FILL, f'fill="{bg_color}"')
    svg = svg.replace(SRC_FG_FILL, f'fill="{fg_color}"')
    out = os.path.join(tmpdir, f"pirate_{network_id}.svg")
    with open(out, "w", encoding="utf-8") as fp:
        fp.write(svg)
    return out


def rasterize_network(tmpdir, network_id, bg_color, fg_color):
    """Recolor the source SVG for a network and rasterize it to a PNG."""
    svg = recolor_svg(tmpdir, network_id, bg_color, fg_color)
    png = os.path.join(tmpdir, f"pirate_{network_id}.png")
    sips_svg_to_png(svg, png, RENDER_HEIGHT)
    return png


def generate_icns(tmpdir, main_raster):
    iconset = os.path.join(tmpdir, "piratecash.iconset")
    os.makedirs(iconset)

    for canvas_px, filename in ICNS_MAP:
        sips_resample_padded(main_raster, os.path.join(iconset, filename), canvas_px)

    icns_out = os.path.join(DIR_OUT, ICNS_OUT)
    subprocess.check_call(["iconutil", "-c", "icns", iconset, "-o", icns_out])
    print(f"Created: {icns_out}")


def check_source(path):
    if not os.path.isfile(path):
        sys.exit(f"Error: Source image not found: {path}")


def main():
    if platform.system() != "Darwin":
        sys.exit("Error: This script requires macOS (needs sips, iconutil).")

    for tool in ("sips", "iconutil"):
        if shutil.which(tool) is None:
            sys.exit(f"Error: '{tool}' not found. Install Xcode command-line tools.")

    check_source(APP_SVG)
    check_source(TRAY)

    os.makedirs(DIR_OUT, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="piratecash_icons_") as tmpdir:
        # Rasterize each network's recolored artwork once
        rasters = {}
        for network_id, _, bg_color, fg_color in NETWORKS:
            rasters[network_id] = rasterize_network(tmpdir, network_id, bg_color, fg_color)

        # Generate bundle icon from the mainnet artwork
        generate_icns(tmpdir, rasters[NETWORKS[0][0]])

        # Generate runtime icons
        for network_id, dst_name, _, _ in NETWORKS:
            dst = os.path.join(DIR_OUT, dst_name)
            sips_resample_padded(rasters[network_id], dst, 256)
            print(f"Created: {dst}")

    # Generate tray icons
    for canvas_px, filename in TRAY_MAP:
        dst = os.path.join(DIR_OUT, filename)
        sips_svg_to_png(TRAY, dst, canvas_px)
        print(f"Created: {dst}")


if __name__ == "__main__":
    main()
