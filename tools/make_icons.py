#!/usr/bin/env python3
"""Makes the app's UI icons from Lucide (https://lucide.dev, ISC license), the icon set the Themezer website uses, with
Themezer-3DS's icon renderer (its tools/make_icons.py), at the Switch's sizes: every icon at 32 pixels, the size shown
next to text, and the few shown large (on empty pages and in messages) at 96 as well. White strokes on transparent
PNGs in romfs/icons, which the app tints. The PNGs are committed, so a build doesn't need this script.

    python3 tools/make_icons.py [path/to/@iconify-json/lucide/icons.json]

Needs a Themezer-3DS checkout (THEMEZER_3DS, default ../themezer-3ds) and a Python with its tools' packages.
"""
import importlib.util, json, os, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = pathlib.Path(os.environ.get("THEMEZER_3DS", ROOT.parent / "themezer-3ds"))
OUT = ROOT / "themezer-nx" / "romfs" / "icons"
SMALL = 32
LARGE = 96

spec = importlib.util.spec_from_file_location("make_icons_3ds", SRC / "tools" / "make_icons.py")
lucide = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lucide)

# The app's name for an icon, and the Lucide icon it is: the 3DS's choices where it has the icon too.
ICONS = {
    "theme": "paintbrush",
    "pack": "package",
    "splash": "image-play",
    "download": "download",
    "install": "hard-drive-download",
    "check": "check",
    "check_circle": "circle-check",
    "search": "search",
    "search_empty": "search-x",
    "settings": "settings",
    "info": "info",
    "refresh": "refresh-cw",
    "sort": "arrow-down-up",
    "filter": "list-filter",
    "wifi": "wifi",
    "wifi_off": "wifi-off",
    "warning": "triangle-alert",
    "error": "circle-x",
    "trash": "trash-2",
    "eye": "eye",
    "square": "square",
    "square_check": "square-check-big",
    "battery": "battery",
    "battery_charging": "battery-charging",
    "keyboard": "keyboard",
    "power": "power",
    "music": "music",
    "volume": "volume-2",
    "volume_off": "volume-x",
    "queue": "list-checks",
    "save": "save",
    "heart": "heart",
    "calendar": "calendar",
    "user": "user",
    "hash": "hash",
    "image": "image",
}

LARGE_ICONS = ["theme", "pack", "splash", "error", "warning", "search_empty", "wifi_off", "check_circle", "info",
               "queue"]


def main():
    source = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else lucide.DEFAULT_SOURCE
    icons = json.loads(source.read_text())["icons"]
    OUT.mkdir(parents=True, exist_ok=True)
    for old in OUT.glob("*.png"):
        old.unlink()
    for name, which in ICONS.items():
        lucide.render(icons[which]["body"], SMALL).save(OUT / f"{name}.png")
        if name in LARGE_ICONS:
            lucide.render(icons[which]["body"], LARGE).save(OUT / f"{name}_{LARGE}.png")
    print(f"{len(ICONS)} icons ({len(LARGE_ICONS)} also large) in {OUT}")


if __name__ == "__main__":
    main()
