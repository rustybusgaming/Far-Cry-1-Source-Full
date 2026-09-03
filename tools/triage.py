#!/usr/bin/env python3
"""
triage.py -- compile-error census for the CryEngine web port.

Milestone 1 is not "make it link", it is "find out how far we are from
compiling". This script compiles every candidate translation unit in isolation
with clang, classifies each diagnostic, and prints a ranked table of what is
actually blocking the port.

The output is a baseline: a number that should go down every time we fix a
class of problem.

Usage:
    tools/triage.py                 # full census
    tools/triage.py --module CryCommon
    tools/triage.py --json out.json
    tools/triage.py --show windows.h # print real diagnostics for one category
"""

import argparse
import concurrent.futures
import json
import os
import re
import subprocess
import sys
import tempfile
from collections import Counter, defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Mirrors cmake/CryPlatform.cmake. Kept in sync by hand; the two are checked
# against each other by --verify-flags.
DEFINES = [
    "LINUX", "LINUX64", "_LINUX",
    "NOT_USE_BINK_SDK", "NOT_USE_DIVX_SDK", "NOT_USE_PUNKBUSTER_SDK",
    "EXCLUDE_UBICOM_CLIENT_SDK", "NOT_USE_UBICOM_SDK", "_CRY_WEBPORT",
    "_LARGEFILE64_SOURCE", "_FILE_OFFSET_BITS=64", "CS_VERSION_361",
]

SILENCED = [
    "builtin-macro-redefined", "inline-new-delete", "missing-exception-spec",
    "invalid-offsetof", "write-strings", "deprecated-declarations",
    "multichar", "narrowing", "unknown-pragmas", "parentheses",
    "dangling-else", "logical-op-parentheses", "unused-value",
    "null-conversion",
]

# Promoted to errors in the real build too -- keep in sync with CryPlatform.cmake.
PROMOTED = ["-Werror=implicit-function-declaration", "-Werror=return-type"]

# Diagnostic classification. Order matters: first match wins, so the most
# specific patterns come first. Each entry is (category, regex, remedy).
RULES = [
    ("windows.h missing",
     r"'(windows|windef|winbase|winsock2?|wtypes|objbase|ole2|shlobj|"
     r"tchar|mmsystem|process|io|direct|conio)\.h' file not found",
     "needs a Win32 shim header or a POSIX rewrite of the caller"),

    ("DirectX / GL SDK header missing",
     r"'(d3d8|d3d9|d3dx9|ddraw|dinput|dsound|d3dx8|gl/gl|GL/gl"
     r"|windows/gl)\w*\.h' file not found",
     "renderer/audio backend -- out of scope until Milestone 4"),

    ("middleware header missing",
     r"'(bink|rad|punkbuster|pb|divx|cg|cgGL|cgD3D|ijl|nvdxt|fmod)\w*\.h'"
     r" file not found",
     "closed-source middleware, must be stubbed or replaced"),

    ("other missing header",
     r"'[^']+\.h(pp)?' file not found",
     "include path or a header that does not exist on this platform"),

    ("MSVC inline assembly",
     r"(__asm|_asm)\b|expected '\(' after 'asm'",
     "x86 asm -- rewrite in C or as an intrinsic; illegal in wasm"),

    ("MSVC keyword / extension",
     r"unknown type name '(__int8|__int16|__int32|__int64|__w64)'"
     r"|__declspec|__based|__unaligned|expected unqualified-id.*__",
     "add a shim to LinuxSpecific.h"),

    ("Win32 type undeclared",
     r"unknown type name '(HWND|HANDLE|HINSTANCE|HMODULE|LPSTR|LPCSTR|"
     r"BOOL|BYTE|WORD|UINT|LONG|ULONG|HRESULT|LPARAM|WPARAM|CRITICAL_SECTION|"
     r"SOCKET|FILETIME|SYSTEMTIME|POINT|RECT|MSG|WNDPROC|GUID|LARGE_INTEGER)'",
     "extend the Win32 type shims in LinuxSpecific.h"),

    ("Win32 API undeclared",
     r"use of undeclared identifier '(GetTickCount|Sleep|CreateThread|"
     r"InitializeCriticalSection|EnterCriticalSection|LeaveCriticalSection|"
     r"CreateEvent|SetEvent|WaitForSingleObject|OutputDebugString|"
     r"MessageBox|GetModuleHandle|LoadLibrary|GetProcAddress|"
     r"QueryPerformanceCounter|InterlockedIncrement|InterlockedDecrement|"
     r"_stricmp|_strnicmp|stricmp|strnicmp|itoa|_itoa|strlwr|_strlwr|"
     r"strupr|_strupr|_snprintf|_vsnprintf|_finite|_isnan|_alloca)'",
     "POSIX equivalent or a shim function"),

    ("STLPORT / STL mismatch",
     r"stlport|_STLP_|no template named '(hash_map|hash_set)'"
     r"|no member named 'hash_map'",
     "retarget onto libc++/unordered_map"),

    ("pre-standard C++ / template lookup",
     r"use of undeclared identifier|no member named|no type named"
     r"|is not a class, namespace, or enumeration"
     r"|missing 'typename' prior to dependent type",
     "two-phase lookup: MSVC 7.1 was permissive, clang is not"),

    ("narrowing / conversion error",
     r"cannot initialize|no viable conversion|assigning to .* from"
     r"|invalid conversion",
     "explicit cast or a type fix"),

    ("syntax / parse error",
     r"expected|extraneous closing brace|unexpected",
     "usually a downstream effect of an earlier failure"),
]

# Translation units that are Win32-by-design and are NOT targets for the port.
# They are excluded from the score rather than counted as failures, because
# counting them makes the number permanently unreachable and hides real
# progress. Each needs a genuine replacement (Milestone 2/3), not a shim.
EXCLUDED = {
    "LuaDebugger/":       "Win32 GUI debugger (commctrl, HWND message pumps)",
    "SystemWin32.cpp":    "raw Win32: registry, message pump, MessageBox",
    "DebugCallStack.cpp": "dbghelp.dll stack walking",
    "Mailer.cpp":         "MAPI crash reporter",
    "SourceSafeHelper.cpp": "Visual SourceSafe COM automation",
    "HTTPDownloader.cpp": "WinInet; becomes fetch() on the web",
    "DownloadManager.cpp": "WinInet; becomes fetch() on the web",
    "getdxver.cpp":       "DirectX version probe via COM",

    # DirectInput device implementations, REPLACED (not shimmed) by the
    # browser backend in CryInput/WebInput.cpp -- see its header for why
    # emulating DirectInput was rejected. XGamepad is Xbox-only: IGamepad
    # itself is declared inside an #ifdef _XBOX in IInput.h. A browser
    # Gamepad API backend would be a separate device, like WebInput.
    "CryInput/XKeyboard.cpp": "DirectInput device; replaced by WebInput.cpp",
    "CryInput/XMouse.cpp":    "DirectInput device; replaced by WebInput.cpp",
    "CryInput/XGamepad.cpp":  "Xbox-only (IGamepad is _XBOX-guarded)",

    # Win32 OVERLAPPED (asynchronous) file I/O: CreateEvent + ReadFileEx +
    # GetOverlappedResult + CancelIo + SleepEx alertable waits. There is no
    # honest shim for this -- it is a real async-I/O redesign, and on the web
    # it has to become either a worker thread or an async fetch pipeline.
    # Stubbing it would compile and then silently stream nothing.
    "RefReadStream.cpp":      "Win32 overlapped I/O; needs an async redesign",
    "RefReadStreamProxy.cpp": "Win32 overlapped I/O; needs an async redesign",
    "RefStreamEngine.cpp":    "Win32 overlapped I/O; needs an async redesign",

    # CryCommon headers that are valid only on a platform we are not building.
    # Compiling them here would be meaningless, not progress.
    "Win32specific.h":    "Win32 platform header (we build the LINUX seam)",
    "Win64specific.h":    "Win64 platform header (we build the LINUX seam)",
    "XboxSpecific.h":     "Xbox platform header",
    "Linux32Specific.h":  "32-bit Linux variant; the port targets LINUX64",
    "_TinyWindow.h":      "Win32 common-controls GUI wrapper (commctrl)",

    # A byte-identical stale copy of CryPhysics/CryPhysics.h that nothing in
    # CryCommon or CrySystem includes. Its "#include utils.h" only resolves as
    # a sibling inside the CryPhysics module, so it is that module's header,
    # not a CryCommon interface, and belongs to the CryPhysics port.
    "CryCommon/CryPhysics.h": "stale duplicate of CryPhysics/CryPhysics.h",

    # CryAISystem is INCOMPLETE in this source drop. The core AI classes --
    # CAISystem, AIObject, AIPlayer and GoalOp -- are absent entirely, headers
    # AND sources; only the graph/pathfinding helpers survive. These five TUs
    # include those missing headers, so no amount of porting can build them.
    # This is a gap upstream, not a port problem.
    "CryAISystem/Graph.cpp":        "needs CAISystem.h, absent from this drop",
    "CryAISystem/GraphUtility.cpp": "needs CAISystem.h, absent from this drop",
    "CryAISystem/Heuristic.cpp":    "needs AIObject.h, absent from this drop",
    "CryAISystem/PipeUser.cpp":     "needs CAISystem.h, absent from this drop",
    "CryAISystem/Puppet.cpp":       "needs CAISystem.h, absent from this drop",
}


def excluded_reason(path):
    for key, why in EXCLUDED.items():
        if key in path:
            return why
    return None


def classify(stderr: str):
    """Return (category, remedy) for the first hard error in stderr."""
    for line in stderr.splitlines():
        if ": error:" not in line and ": fatal error:" not in line:
            continue
        for cat, pat, remedy in RULES:
            if re.search(pat, line, re.IGNORECASE):
                return cat, remedy, line.strip()
    for line in stderr.splitlines():
        if ": error:" in line or ": fatal error:" in line:
            return "unclassified", "needs manual inspection", line.strip()
    return None, None, None


def compile_one(job):
    kind, path, include_dirs = job
    with tempfile.NamedTemporaryFile("w", suffix=".cpp", delete=False) as f:
        if kind == "header":
            rel = os.path.relpath(path, os.path.join(ROOT, "CryCommon"))
            f.write("#include <platform.h>\n")
            f.write('#include "%s"\n' % rel)
            tu = f.name
        else:
            tu = None
    src = tu if kind == "header" else path

    # Mirror cmake/CryPlatform.cmake exactly. Using a blanket -w here would
    # let a TU be scored as passing that the real build then rejects: the
    # build promotes a few diagnostics (return-type, implicit function
    # declaration) to errors precisely because they are undefined behaviour,
    # not style.
    cmd = ["clang++", "-fsyntax-only", "-std=gnu++98"]
    cmd += ["-D%s" % d for d in DEFINES]
    cmd += ["-Wno-%s" % w for w in SILENCED]
    cmd += PROMOTED
    cmd += ["-I%s" % d for d in include_dirs]
    cmd += [src]

    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        rc, err = p.returncode, p.stderr
    except subprocess.TimeoutExpired:
        rc, err = 1, "timeout"
    finally:
        if tu:
            os.unlink(tu)

    nerr = len(re.findall(r": (?:fatal )?error:", err))
    cat, remedy, sample = classify(err) if rc != 0 else (None, None, None)
    return {
        "kind": kind,
        "path": os.path.relpath(path, ROOT),
        "ok": rc == 0,
        "errors": nerr,
        "category": cat,
        "remedy": remedy,
        "sample": sample,
        "stderr": err if rc != 0 else "",
    }


# Per-module include paths. A module compiles against CryCommon (always) plus
# its own directory and whatever vendored subdirectories it carries.
MODULES = {
    "CryCommon":   {"kind": "headers", "incs": []},
    "CrySystem":   {"kind": "sources", "incs": ["XML", "zlib"]},
    "Cry3DEngine": {"kind": "sources", "incs": []},
    "CryAnimation":{"kind": "sources", "incs": []},
    "CryEntitySystem": {"kind": "sources", "incs": []},
    "CryPhysics":  {"kind": "sources", "incs": []},
    "CryMovie":    {"kind": "sources", "incs": ["xml", "xml/Expat"]},
    # FreeType's ft2build.h includes <ftheader.h> by bare name, so its config
    # directory has to be on the include path, not just include/.
    "CryFont":     {"kind": "sources",
                    "incs": ["FreeType2/include", "FreeType2/include/freetype/config",
                             "FreeType2"]},
    "CryInput":    {"kind": "sources", "incs": []},
    "CryNetwork":  {"kind": "sources", "incs": []},
    "CryAISystem": {"kind": "sources", "incs": []},
    # vorbisfile.h is included by bare name, so OggVorbisInclude/vorbis has
    # to be on the path as well as OggVorbisInclude.
    "CrySoundSystem": {"kind": "sources",
                       "incs": ["OggVorbisInclude", "OggVorbisInclude/vorbis",
                                "OggVorbisInclude/ogg"]},
    "CryScriptSystem": {"kind": "sources", "incs": ["LUA", "LUA/lib"]},

    # The renderer. RenderDll/ itself holds the shared precompiled header, and
    # Common/ holds the backend-independent renderer (shaders, textures, render
    # elements). The per-backend directories are separate modules below.
    "RenderDll/Common": {"kind": "sources",
                         "incs": ["..", "Textures", "Textures/Image",
                                  "Shaders", "RendElements", "NvTriStrip", "3Dc"]},
    "RenderDll/XRenderNULL": {"kind": "sources",
                              "incs": ["..", "../Common", "../Common/Textures",
                                       "../Common/Shaders", "../Common/RendElements"]},
}


def gather(modules):
    common = os.path.join(ROOT, "CryCommon")
    jobs = []
    for mod in modules:
        cfg = MODULES.get(mod)
        if cfg is None:
            print("unknown module %r (known: %s)"
                  % (mod, ", ".join(sorted(MODULES))), file=sys.stderr)
            continue
        moddir = os.path.join(ROOT, mod)
        if not os.path.isdir(moddir):
            print("no such directory: %s" % moddir, file=sys.stderr)
            continue

        incs = [common, moddir] + [os.path.join(moddir, i) for i in cfg["incs"]]

        if cfg["kind"] == "headers":
            # A pure interface module: compile each header as its own TU.
            for h in sorted(os.listdir(moddir)):
                if h.endswith(".h"):
                    jobs.append(("header", os.path.join(moddir, h), incs))
        else:
            for dirpath, _, files in os.walk(moddir):
                for fn in sorted(files):
                    if fn.endswith(".cpp"):
                        jobs.append(("source", os.path.join(dirpath, fn), incs))
    return jobs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--module", action="append", default=None)
    ap.add_argument("--json")
    ap.add_argument("--show", help="print full diagnostics for one category")
    ap.add_argument("--excluded", action="store_true",
                    help="list the TUs excluded by design and why")
    ap.add_argument("-j", type=int, default=os.cpu_count() or 4)
    args = ap.parse_args()

    modules = args.module or ["CryCommon", "CrySystem"]
    jobs = gather(modules)
    if not jobs:
        print("no translation units found", file=sys.stderr)
        return 1

    print("triaging %d translation units with %d workers...\n" % (len(jobs), args.j))
    with concurrent.futures.ThreadPoolExecutor(args.j) as ex:
        results = list(ex.map(compile_one, jobs))

    if args.excluded:
        for r in results:
            why = excluded_reason(r["path"])
            if why:
                print("  %-44s %s" % (r["path"], why))
        return 0

    if args.show:
        for r in results:
            if r["category"] == args.show:
                print("=" * 78)
                print(r["path"])
                print("-" * 78)
                print(r["stderr"][:4000])
        return 0

    for r in results:
        r["excluded"] = excluded_reason(r["path"])

    by_kind = defaultdict(lambda: [0, 0])
    for r in results:
        if r["excluded"]:
            continue
        mod = r["path"].split("/")[0]
        k = "%s %s" % (mod, "headers" if r["kind"] == "header" else "sources")
        by_kind[k][1] += 1
        if r["ok"]:
            by_kind[k][0] += 1

    print("=" * 78)
    print("  COMPILE CENSUS -- clang %s, -std=gnu++98, LINUX64 seam"
          % subprocess.run(["clang++", "-dumpversion"], capture_output=True,
                           text=True).stdout.strip())
    print("=" * 78)
    for k, (ok, tot) in sorted(by_kind.items()):
        pct = 100.0 * ok / tot if tot else 0
        bar = "#" * int(pct / 4) + "." * (25 - int(pct / 4))
        print("  %-20s %4d/%-4d  [%s] %5.1f%%" % (k, ok, tot, bar, pct))

    tot_ok = sum(1 for r in results if r["ok"] and not r["excluded"])
    tot = sum(1 for r in results if not r["excluded"])
    print("  %-20s %4d/%-4d" % ("TOTAL", tot_ok, tot))

    nex = sum(1 for r in results if r["excluded"])
    if nex:
        print("\n  (%d further TUs excluded by design -- Win32-only, see --excluded)"
              % nex)

    cats = Counter(r["category"] for r in results
                   if not r["ok"] and not r["excluded"])
    if cats:
        print("\n" + "=" * 78)
        print("  BLOCKERS BY CATEGORY")
        print("=" * 78)
        remedies = {r["category"]: r["remedy"] for r in results if not r["ok"]}
        for cat, n in cats.most_common():
            print("  %3d TU  %s" % (n, cat))
            print("          -> %s" % remedies.get(cat, ""))

        print("\n" + "=" * 78)
        print("  WORST OFFENDERS (by error count)")
        print("=" * 78)
        for r in sorted((r for r in results
                         if not r["ok"] and not r["excluded"]),
                        key=lambda r: -r["errors"])[:12]:
            print("  %5d  %-46s %s" % (r["errors"], r["path"], r["category"]))

    passing = sorted(r["path"] for r in results if r["ok"])
    print("\n%d translation units already compile clean." % len(passing))

    if args.json:
        with open(args.json, "w") as f:
            json.dump({"results": results,
                       "summary": {k: v for k, v in by_kind.items()}}, f, indent=2)
        print("wrote %s" % args.json)
    return 0


if __name__ == "__main__":
    sys.exit(main())
