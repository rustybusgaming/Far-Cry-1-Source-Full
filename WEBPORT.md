# CryEngine 1.33 / Far Cry — web port

Porting the Far Cry engine to run in a browser (WebAssembly + WebGL2).

This document covers **Milestone 1: get the engine through a non-MSVC
toolchain.** Nothing here renders a frame yet; this is the foundation work that
everything else is blocked behind.

---

## Current state

```
CryCommon headers      66/130   50.8%
CrySystem sources      14/58    24.1%
TOTAL                  80/188
```

Starting point was **1/188**. The build is green: `libCrySystem.a` links.

| Pass | Fix | Total |
|---|---|---|
| baseline | — | 1/188 |
| 1 | `WinBase.h` shim + `stdafx.h` case | 40/188 |
| 2 | math template cluster ordering | 54/188 |
| 3 | `Cry_Geo.h` shadowing bug, `fopen_nocase` | 59/188 |
| 4 | `GetPlane` / `GetTransposed44` friend visibility, `Snap_s180` | 72/188 |
| 5 | `XDOM` forward decl, self-contained headers | 80/188 |

---

## Building

```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

Census of what does and does not compile:

```bash
tools/triage.py                       # ranked table of remaining blockers
tools/triage.py --show "windows.h missing"   # real diagnostics for one category
tools/triage.py --json out.json
```

The wasm target is wired but not yet the working target — see
*Why native clang first* below.

```bash
source /path/to/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-wasm -G Ninja
```

---

## How this is structured

The original tree is Visual Studio 2005 / Windows only. **The CMake build is
additive** — no `.vcproj` or `.sln` file was touched, so the MSVC build still
works exactly as before.

```
CMakeLists.txt                  top level, module list
cmake/CryPlatform.cmake         platform defines, warning policy
cmake/CryModule.cmake           cry_add_module(), cry_add_header_gate()
cmake/toolchains/Emscripten.cmake   wasm settings
tools/triage.py                 the compile census
CryCommon/WinBase.h             NEW — the missing Win32 shim
CryCommon/Cry_MathFwd.h         NEW — math cluster forward declarations
```

Both module lists (`CryCommon/CMakeLists.txt`, `CrySystem/CMakeLists.txt`) are
**explicit allowlists, not globs**. That is deliberate: the build stays green,
so any new breakage is a real regression, and moving a file into the list is
the unit of progress.

### The header gate

`CryCommon` is 130 headers and zero `.cpp` files. A header that is never
compiled is never validated, so the gate synthesises one TU per header:

```cpp
#include <platform.h>
#include "TheHeader.h"
```

Compiling each header *in isolation* rather than all of them together is the
point — it proves each one is self-contained, and turns a binary
"builds / doesn't build" into a countable score.

### Why native clang first

Milestone 1 targets clang natively, not wasm. The first job is getting ~800k
lines of MSVC-era C++ through a conforming front end, and native clang gives
the same diagnostics as `emcc` for a fraction of the build time and with a
working debugger. **Everything fixed for clang is fixed for emcc.** The
Emscripten toolchain file exists so the wasm target is ready when the code is.

The port reuses the engine's own `LINUX` seam rather than inventing a new one.
Crytek shipped a Linux dedicated server, so `platform.h` and `ProjectDefines.h`
already branch on it and already disable the closed-source middleware. To a
first approximation the web port *is* the Linux build, retargeted to wasm.

---

## What was actually wrong

Nearly everything traced to a handful of root causes, each affecting dozens of
files. The pattern held every pass: a category with 60–138 translation units in
it was one bug, not sixty.

### 1. The missing `WinBase.h` (138 TUs)

`platform.h` ends with:

```c
#if defined(LINUX)
    #include <WinBase.h>
#endif
```

Crytek's Linux build had a hand-written `WinBase.h` supplying the slice of
Win32 the engine touches. **It was never part of the released source drop**, so
the `LINUX` branch has been dangling ever since — every header that includes
`platform.h` died on this one missing include.

`CryCommon/WinBase.h` is a reconstruction: handle types, `CRITICAL_SECTION` on
recursive pthread mutexes, `Interlocked*` on atomic builtins, timing, and
`fopen_nocase`. It is intentionally **not** general-purpose Win32 emulation —
it covers what the engine calls and nothing else, so unported code fails loudly
at compile time instead of silently binding to a wrong stub.

It layers *under* what already exists and does not shadow it:

| Symbol | Already defined in | Note |
|---|---|---|
| `HANDLE` | `LinuxSpecific.h` | `CHandle<int,-1>` — an fd, not a `void*` |
| `INVALID_HANDLE_VALUE` | `LinuxSpecific.h` | an enum constant |
| `HMODULE` | `CryMemoryManager.h` | `void*`, next to its `<dlfcn.h>` |

### 2. `fopen_nocase` was called but never existed

`ILog.h` calls it under `#if defined(LINUX)`; it is defined nowhere in the tree
— same missing Linux layer.

This one is not a nicety. Far Cry's data is authored on Windows, where the
filesystem is case-insensitive, and the asset references are correspondingly
inconsistent: a `.lua` asks for `Textures/Sky.dds`, the pak entry says
`textures/sky.dds`, a level file says `TEXTURES/SKY.DDS`. All three resolve on
NTFS and produce three different failures on ext4 — **and on Emscripten's
MEMFS/IDBFS, which are case-sensitive too.** Without it the engine loads
nothing. The implementation tries the literal path first and only falls back to
a per-component case-insensitive walk on failure.

### 3. The math template cluster (88 TUs)

`Cry_Vector2/3`, `Cry_Quat` and `Cry_Matrix` are mutually recursive, and some
need *complete* types from each other — `Quaternion_tpl` stores a `Vec3` by
value. Exactly one include order satisfies all of them, and `Cry_Math.h` is
what establishes it. Entering the cluster anywhere else (which plenty of engine
headers do) starts the cycle at the wrong point.

Fixed by hoisting `#include "Cry_Math.h"` **above the include guard** in each of
the four headers. Above, not below: below the guard, the cluster recursing back
into the header would find the guard already set, skip the body, and leave the
type incomplete — the exact deadlock that made these unbuildable standalone.

### 4. Friend functions defined inside classes (65 + 60 TUs)

A friend *defined* inside a class is invisible to ordinary lookup — it can only
be found by ADL on its own parameter types. `GetPlane(const Vec3&, const Vec3&)`
is a friend of `Plane`, so ADL searches `Vec3`'s scope and never finds it.
`GetTransposed44` is a friend of `Matrix44_tpl` but gets called with a
`Matrix33` argument.

MSVC 7.1 implemented the pre-standard rule that injected friend names into the
enclosing namespace, which is why this ever compiled. Fixed with namespace-scope
declarations; the definitions stay where they are.

### 5. Two-phase lookup (`Snap_s180`)

`Ang3_tpl::Snap180` calls `Snap_s180(this->x)` ~55 lines before that function is
declared. The call is non-dependent and `f32` is a fundamental type with no
associated namespace, so ADL cannot rescue it. MSVC deferred the lookup to
instantiation; clang requires it visible at definition.

### 6. A real bug, latent since 2004

```cpp
ILINE void SetOBB( const Matrix33& m33, const Vec3& hlv, const Vec3& center )
{  m33=m33; h=hlv; c=center; }
```

The parameter `m33` **shadows the member** `m33`, so this self-assigns the const
parameter and never sets the member. `SetOBB()` left the orientation matrix
uninitialised. It survived twenty years because `OBB_tpl` is a class template
and this member was never instantiated, so the body was never type-checked.
Fixed to `this->m33 = m33`, matching `CreateOBB()` immediately below.

### 7. Case-sensitivity and non-self-contained headers

47 sources spelled `#include "stdafx.h"`; the file is `StdAfx.h`. Fine on
Windows, fatal on Linux.

Several headers (`ColorDefs.h`, `IBindable.h`, `AnimKey.h`,
`CryEngineDecalInfo.h`) named engine math types while including *nothing*,
relying on always being compiled after `Cry_Math.h` in the `.vcproj`'s fixed
order. Each was given its actual dependency, verified individually.

Also `struct XDOM::IXMLDOMDocument;` (40 TUs) — a qualified name cannot be
forward-declared; it has to be declared inside its namespace.

---

## What's next

### Milestone 1 remainder — 108 TUs left

Run `tools/triage.py` for the live list. The largest remaining groups are
render-element headers (`CREOcLeaf`, `CRESky`, `CRETerrainSector`,
`RendElement`) that need `RendElement.h` visible, and `AABBSV.h` / `primitives.h`
with more lookup-order issues. Expect the same pattern — a few root causes, not
a hundred.

One find worth flagging: `CryHeaders.h:387` casts a pointer to `int`, losing
bits on any 64-bit build. Harmless on wasm32, where pointers are 32-bit, but it
is a genuine latent bug.

### Milestone 2 — platform layer

Replace, don't shim: Win32 threading, file I/O and timers; strip or
intrinsic-ify the 21 files containing inline x86 `__asm` (illegal in wasm);
`<ext/hash_map>` → `unordered_map`. The excluded CrySystem files are listed in
`CrySystem/CMakeLists.txt` with the reason for each.

### Milestone 3 — headless build

Stub renderer/sound/video behind interfaces and target a NULL-render build. A
dedicated server in wasm is a genuinely reachable target and proves out
everything except the renderer.

The engine's `while (!quit)` main loop cannot work on the browser's event loop.
Asyncify is enabled as the short-term answer; restructuring around
`emscripten_set_main_loop` is the real fix.

### Milestone 4 — the renderer

The bulk of the project. `XRenderOGL` is OpenGL 1.x: 74 `glBegin` sites, 44
references to `GL_NV_register_combiners`, WGL context creation, and NVIDIA Cg
shaders whose `cgGL.lib` is **a binary blob with no source**. WebGL2 supports
none of it. Every shader and the whole fixed-function/combiner pipeline needs
rewriting to GLSL ES.

### Known hard blockers

| Blocker | Status |
|---|---|
| Cg shaders (`cgGL.lib`) | binary only — full shader rewrite required |
| Sound (`crysound.lib`) | binary only; it is **FMOD 3.61** rebranded (`CS_SAMPLE`, `CS_STREAM`, `CS_DSPUNIT`). Must be reimplemented over OpenAL/WebAudio |
| Bink video (`binkw32.dll`) | binary only — cutscenes need re-encoding |
| Networking | browsers cannot open raw sockets; needs a WebSocket/WebRTC relay |
| Game assets | several GB of `.pak`; a streaming problem, and only distributable to people who own the game |

---

## Licensing

This tree is Crytek-copyrighted, **not** open source. The bundled licence
permits free, non-commercial modification and redistribution of the SDK, which
covers a hobby port — but game assets cannot be redistributed, and a commercial
release needs Crytek's agreement.
