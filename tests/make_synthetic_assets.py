#!/usr/bin/env python3
"""Build a synthetic Far Cry asset tree.

WHAT THIS IS AND IS NOT

It is NOT game data. Every file it writes is a placeholder written by this
script: an empty zip, an empty font element, a two-line Lua file. There is no
Far Cry content here and there never can be -- the real assets are several GB
and are not redistributable, so they cannot be in this repository, in a build
artifact, or in CI.

What it IS: enough structure for the engine to recognise an asset root and
open a file from it. That is the thing worth testing, and it is testable
precisely because the engine does not care what is inside the files at the
point where it decides where to look.

Used by the asset_root test, and useful by hand when working on the host:

    tests/make_synthetic_assets.py /tmp/fake && build/Headless/Headless --data /tmp/fake
"""

import os
import sys
import zipfile


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text)


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: make_synthetic_assets.py <dir>")

    root = sys.argv[1]
    os.makedirs(root, exist_ok=True)

    write(os.path.join(root, "scripts", "main.lua"),
          "-- Synthetic placeholder. No game content.\n")

    # The engine loads these before it will draw text. An empty element is
    # enough for the file to exist and be opened; it is not enough to produce
    # a font, which is correct -- this tree is about paths, not rendering.
    for name in ("default.xml", "console.xml"):
        write(os.path.join(root, "languages", "fonts", name), "<fonts/>\n")

    # A REAL zip, because CryPak opens these with a zip reader and a stub file
    # would fail for the wrong reason -- "not an archive" rather than "no such
    # archive", which is exactly the confusion this test exists to avoid.
    pak = os.path.join(root, "FCData", "Localized", "english.pak")
    os.makedirs(os.path.dirname(pak), exist_ok=True)
    with zipfile.ZipFile(pak, "w") as z:
        z.writestr("placeholder.txt", "synthetic, no game content\n")

    print("synthetic asset tree at %s" % os.path.abspath(root))


if __name__ == "__main__":
    main()
