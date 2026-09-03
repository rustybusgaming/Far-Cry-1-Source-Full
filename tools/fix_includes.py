#!/usr/bin/env python3
"""
fix_includes.py -- repair #include directives that only work on Windows.

Two failure modes, both invisible on NTFS and both fatal on Linux, on macOS
with a case-sensitive volume, and in Emscripten's filesystems:

  1. Backslash separators.   #include "XML\\Xml.h"
     A backslash is not a path separator anywhere but Windows. Worse, in a
     C string "\\x" is an escape sequence, so this is not even portable C.

  2. Case mismatches.        #include "CrySound.h"   (file is crysound.h)
                             #include "winbase.h"    (file is WinBase.h)
     NTFS is case-insensitive, so the wrong spelling resolves fine on the
     original build machines and nowhere else.

The tool resolves every quoted include against the real files on disk, matching
case-insensitively, and rewrites the directive to the actual name. An include
that already resolves exactly is left alone; one that cannot be resolved at all
is reported rather than guessed at, since that usually means a genuinely absent
header (a Win32 SDK or middleware include) rather than a typo.

    tools/fix_includes.py CryCommon CrySystem            # report
    tools/fix_includes.py CryCommon CrySystem --apply
"""

import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
INCLUDE_RE = re.compile(r'^(\s*#\s*include\s*)"([^"]+)"(.*)$')

# Angle includes are handled too, but conservatively: an angle include is only
# rewritten when it does NOT already name a real file and DOES match a project
# header case-insensitively. That keeps <stdio.h>, <vector> and every other
# system/toolchain header untouched, while still catching the engine's own
# <CrySound.h> (the file is crysound.h) and <winbase.h> (it is WinBase.h).
ANGLE_RE = re.compile(r'^(\s*#\s*include\s*)<([^>]+)>(.*)$')


def build_index(dirs):
    """{module root: {lowercased path -> actual path}}.

    Indexed PER ROOT rather than globally. Several modules ship their own
    StdAfx.h with different capitalisation (CryEntitySystem/stdafx.h vs
    CryMovie/StdAfx.h); a single shared index lets one module's spelling
    shadow another's, so CryMovie/xml/xml.cpp would "resolve" against
    CryEntitySystem's copy and be left alone. Includes are resolved against
    the including file's own module first.
    """
    index = {}
    for d in dirs:
        per = {}
        for dirpath, _, files in os.walk(d):
            for fn in files:
                if not fn.lower().endswith((".h", ".hpp", ".inl", ".c", ".cpp")):
                    continue
                rel = os.path.relpath(os.path.join(dirpath, fn), d).replace(os.sep, "/")
                per.setdefault(rel.lower(), rel)
                per.setdefault(fn.lower(), fn)
        index[d] = per
    return index


def lookup(index, srcdir, key):
    """Search the including file's own module root first, then the others."""
    own = [d for d in index if srcdir == d or srcdir.startswith(d + os.sep)]
    for d in own + [d for d in index if d not in own]:
        hit = index[d].get(key)
        if hit:
            return hit
    return None


def walk_case_insensitive(base, relpath):
    """Resolve relpath under base, matching each component case-insensitively.
    Returns the corrected relative path (preserving '..'), or None."""
    parts = [c for c in relpath.split("/") if c != ""]
    cur = base
    out = []
    for comp in parts:
        if comp in (".", ".."):
            cur = os.path.normpath(os.path.join(cur, comp))
            out.append(comp)
            continue
        if not os.path.isdir(cur):
            return None
        try:
            entries = os.listdir(cur)
        except OSError:
            return None
        hit = None
        for e in entries:
            if e.lower() == comp.lower():
                hit = e
                break
        if hit is None:
            return None
        out.append(hit)
        cur = os.path.join(cur, hit)
    if not os.path.isfile(cur):
        return None
    return "/".join(out)


def resolve(spec, index, srcdir):
    """Return the corrected include spec, or None if already fine / unresolvable.

    Resolution order matters. We first try the path as written, relative to the
    including file -- that keeps any '..' or subdirectory prefix intact and only
    corrects the CASE of each component. Only if that fails do we fall back to
    the project-wide index, and last of all to a bare filename. Doing it the
    other way round silently discards directory components, turning
    "Expat/expat.h" into "expat.h" and "../resource.h" into "resource.h",
    which happen to work only if those directories are also on the -I path.
    """
    norm = spec.replace("\\", "/")

    # Correct as written?
    if norm == spec and os.path.isfile(os.path.join(srcdir, norm)):
        return None

    # Case-correct, relative to the including file, preserving structure.
    fixed = walk_case_insensitive(srcdir, norm)
    if fixed:
        return fixed if fixed != spec else None

    # Module index, full relative path -- own module first.
    key = norm.lower()
    hit = lookup(index, srcdir, key)
    if hit and hit != spec:
        return hit

    # Last resort: bare filename (loses directory structure, so only used
    # when nothing else resolved).
    hit = lookup(index, srcdir, key.rsplit("/", 1)[-1])
    if hit and hit != spec:
        return hit

    if norm != spec:
        return norm          # at minimum, fix the separator
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dirs", nargs="+")
    ap.add_argument("--apply", action="store_true")
    args = ap.parse_args()

    dirs = [os.path.join(ROOT, d) for d in args.dirs]
    index = build_index(dirs)

    changed = unresolved = 0
    for d in dirs:
        for dirpath, _, files in os.walk(d):
            for fn in sorted(files):
                if not fn.endswith((".h", ".cpp", ".inl")):
                    continue
                path = os.path.join(dirpath, fn)
                try:
                    lines = open(path, errors="surrogateescape").read().split("\n")
                except OSError:
                    continue

                dirty = False
                for i, line in enumerate(lines):
                    m = INCLUDE_RE.match(line)
                    angle = False
                    if not m:
                        m = ANGLE_RE.match(line)
                        angle = True
                    if not m:
                        continue
                    pre, spec, post = m.groups()

                    if angle:
                        # Only touch angle includes that name a project header
                        # under a different spelling. Anything that resolves as
                        # written, or that we have no file for, is left alone.
                        key = spec.replace("\\", "/").lower()
                        cand = (lookup(index, dirpath, key)
                                or lookup(index, dirpath, key.rsplit("/", 1)[-1]))
                        fixed = cand if (cand and cand != spec) else None
                    else:
                        fixed = resolve(spec, index, dirpath)
                    if fixed and fixed != spec:
                        rel = os.path.relpath(path, ROOT)
                        print("  %s:%d\n      %-34s -> %s" % (rel, i + 1, spec, fixed))
                        lines[i] = ('%s<%s>%s' if angle else '%s"%s"%s') % (
                            pre, fixed, post)
                        dirty = True
                        changed += 1
                    elif "\\" in spec:
                        unresolved += 1

                if dirty and args.apply:
                    open(path, "w", errors="surrogateescape").write("\n".join(lines))

    print("\n%d includes corrected%s" % (changed, "" if args.apply else " (dry run)"))
    if unresolved:
        print("%d backslash includes could not be resolved to a real file"
              % unresolved)
    return 0


if __name__ == "__main__":
    sys.exit(main())
