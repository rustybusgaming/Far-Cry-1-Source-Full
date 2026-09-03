#!/usr/bin/env python3
"""
selfcontain.py -- make engine headers self-contained, empirically.

Many CryEngine headers name types they never include. They were written for a
single .vcproj compile order in which some earlier header had always already
pulled the dependency in, so the omission was invisible. Any build that compiles
them in a different order -- ours, and any header gate -- fails.

Rather than guess, this brute-forces it: for each failing header, try adding one
candidate include (then pairs), recompile, and keep the first combination that
makes the header compile clean. A change is only written if it actually works,
so the tool cannot make things worse.

    tools/selfcontain.py --module CryCommon            # report only
    tools/selfcontain.py --module CryCommon --apply    # write the fixes
"""

import argparse
import itertools
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFINES = ["LINUX", "LINUX64", "_LINUX", "NOT_USE_BINK_SDK", "NOT_USE_DIVX_SDK",
           "NOT_USE_PUNKBUSTER_SDK", "EXCLUDE_UBICOM_CLIENT_SDK", "_CRY_WEBPORT"]

# Ordered by how often they turn out to be the missing dependency. Cheap ones
# and common bases first, so the search usually terminates on the first try.
CANDIDATES = [
    "Cry_Math.h",        # Vec3, Matrix, Quat -- by far the most common
    "CryHeaders.h",      # chunk/file format structs, CryFace
    "RendElement.h",     # CRendElement, base of every CRE* header
    "Cry_Geo.h",         # AABB, Sphere, Plane
    "VertexFormats.h",
    "ColorDefs.h",
    "ISystem.h",
    "IRenderer.h",
    "ILog.h",
    "Cry_Color.h",
]

NOTE = ("// [webport] This header was not self-contained: it names types it "
        "never\n// included, relying on the .vcproj's fixed compile order to "
        "have pulled\n// them in first. Including its real "
        "dependencies lets it stand alone.\n")


def errors(header, incdir, extra=()):
    """Compile `header` as its own TU with `extra` includes injected first."""
    with tempfile.NamedTemporaryFile("w", suffix=".cpp", delete=False) as f:
        f.write("#include <platform.h>\n")
        for e in extra:
            f.write('#include "%s"\n' % e)
        f.write('#include "%s"\n' % header)
        tu = f.name
    try:
        p = subprocess.run(
            ["clang++", "-fsyntax-only", "-std=gnu++98", "-w"]
            + ["-D" + d for d in DEFINES] + ["-I" + incdir, tu],
            capture_output=True, text=True, timeout=120)
        return len(re.findall(r": (?:fatal )?error:", p.stderr))
    except subprocess.TimeoutExpired:
        return 9999
    finally:
        os.unlink(tu)


def insert_includes(path, incs):
    """Insert includes after the leading comment block, before the first
    directive or code. Returns the new file text."""
    s = open(path).read()
    block = NOTE + "".join('#include "%s"\n' % i for i in incs) + "\n"
    m = re.search(r'^\s*#(ifndef|pragma once|define|include)', s, re.M)
    idx = m.start() if m else 0
    return s[:idx] + block + s[idx:]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--module", default="CryCommon")
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--max-combo", type=int, default=2)
    args = ap.parse_args()

    incdir = os.path.join(ROOT, "CryCommon")
    moddir = os.path.join(ROOT, args.module)
    headers = sorted(h for h in os.listdir(moddir) if h.endswith(".h"))

    fixed, unresolved = [], []
    for h in headers:
        base = errors(h, incdir)
        if base == 0:
            continue

        solution = None
        for n in range(1, args.max_combo + 1):
            for combo in itertools.combinations(CANDIDATES, n):
                if h in combo:
                    continue
                if errors(h, incdir, combo) == 0:
                    solution = combo
                    break
            if solution:
                break

        if solution:
            fixed.append((h, base, solution))
            print("  FIX  %-34s %3d errors -> 0   via %s"
                  % (h, base, ", ".join(solution)))
            if args.apply:
                p = os.path.join(moddir, h)
                new = insert_includes(p, solution)
                open(p, "w").write(new)
                # Verify in place; revert if the real file disagrees with the probe.
                if errors(h, incdir) != 0:
                    print("       ! reverted: in-place result differs")
                    subprocess.run(["git", "checkout", "--", p], cwd=ROOT)
                    fixed.pop()
        else:
            unresolved.append((h, base))

    print("\n%d headers fixed, %d still failing" % (len(fixed), len(unresolved)))
    if unresolved:
        print("\nstill failing (need manual work):")
        for h, n in sorted(unresolved, key=lambda x: -x[1])[:25]:
            print("  %4d  %s" % (n, h))
    if not args.apply and fixed:
        print("\n(dry run -- rerun with --apply to write these)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
