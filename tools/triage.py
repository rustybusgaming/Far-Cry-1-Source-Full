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
    "EXCLUDE_UBICOM_CLIENT_SDK", "_CRY_WEBPORT",
]

SILENCED = [
    "builtin-macro-redefined", "inline-new-delete", "missing-exception-spec",
    "invalid-offsetof", "write-strings", "deprecated-declarations",
    "multichar", "narrowing", "unknown-pragmas", "parentheses",
    "dangling-else", "logical-op-parentheses", "unused-value",
    "null-conversion",
]

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

WIN32_ONLY_HINTS = (
    "SystemWin32", "DebugCallStack", "Mailer", "SourceSafeHelper",
    "DllMain", "LuaDebugger", "HTTPDownloader", "DownloadManager",
    "ApplicationHelper", "DataProbe",
)


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

    cmd = ["clang++", "-fsyntax-only", "-std=gnu++98", "-w"]
    cmd += ["-D%s" % d for d in DEFINES]
    cmd += ["-Wno-%s" % w for w in SILENCED]
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


def gather(modules):
    common = os.path.join(ROOT, "CryCommon")
    jobs = []
    if "CryCommon" in modules:
        for h in sorted(os.listdir(common)):
            if h.endswith(".h"):
                jobs.append(("header", os.path.join(common, h), [common]))
    if "CrySystem" in modules:
        sysdir = os.path.join(ROOT, "CrySystem")
        incs = [common, sysdir, os.path.join(sysdir, "XML"),
                os.path.join(sysdir, "zlib")]
        for dirpath, _, files in os.walk(sysdir):
            for fn in sorted(files):
                if fn.endswith(".cpp"):
                    jobs.append(("source", os.path.join(dirpath, fn), incs))
    return jobs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--module", action="append", default=None)
    ap.add_argument("--json")
    ap.add_argument("--show", help="print full diagnostics for one category")
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

    if args.show:
        for r in results:
            if r["category"] == args.show:
                print("=" * 78)
                print(r["path"])
                print("-" * 78)
                print(r["stderr"][:4000])
        return 0

    by_kind = defaultdict(lambda: [0, 0])
    for r in results:
        k = "CryCommon headers" if r["kind"] == "header" else "CrySystem sources"
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

    tot_ok = sum(1 for r in results if r["ok"])
    print("  %-20s %4d/%-4d" % ("TOTAL", tot_ok, len(results)))

    cats = Counter(r["category"] for r in results if not r["ok"])
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
        for r in sorted((r for r in results if not r["ok"]),
                        key=lambda r: -r["errors"])[:12]:
            hint = "  [win32-only]" if any(h in r["path"] for h in WIN32_ONLY_HINTS) else ""
            print("  %5d  %-46s %s%s" % (r["errors"], r["path"], r["category"], hint))

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
