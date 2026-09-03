# CryEngine 1.33 / Far Cry — web port

Porting the Far Cry engine to run in a browser (WebAssembly + WebGL2).

This document covers **Milestone 1: get the engine through a non-MSVC
toolchain** — now complete for CryCommon and CrySystem. Nothing here renders a
frame yet; this is the foundation work everything else is blocked behind.

---

## Current state

```
CryCommon headers     126/126   100.0%
CrySystem sources      45/45    100.0%
Cry3DEngine sources    73/73    100.0%
CryEntitySystem        12/12    100.0%
CryMovie               23/23    100.0%
CryScriptSystem          9/9    100.0%
CryAISystem              3/3    100.0%
CryInput                 8/8    100.0%
CryNetwork             26/26    100.0%
TOTAL                 325/325
```

Starting point was **1/188**. Nine modules compile completely and the build is
green, producing eight static libraries plus the CryCommon header gate.

They **compile but do not link into a game yet, and are not meant to** — each
calls into modules that are still unported (above all the renderer). Compiling
is this milestone.

27 further translation units are **excluded by design** — Win32-only code that
is not a port target, plus the parts of CryAISystem that are missing from the
source drop entirely. Each carries a reason:

```bash
tools/triage.py --excluded
```

| Pass | Fix | Total |
|---|---|---|
| baseline | — | 1/188 |
| 1 | `WinBase.h` shim + `stdafx.h` case | 40/188 |
| 2 | math template cluster ordering | 54/188 |
| 3 | `Cry_Geo.h` shadowing bug, `fopen_nocase` | 59/188 |
| 4 | friend visibility, `Snap_s180` | 72/188 |
| 5 | `XDOM` forward decl, self-contained headers | 80/188 |
| 6 | include case/separators, `PHYSICS_EXPORTS` | 92/189 |
| 7 | 31 headers made self-contained | 134/174 |
| 8 | path helpers, CRT shims, find API | 154/171 |
| 9 | render header cycle, `XmlParser` iterators | **170/170** |
| 10 | Cry3DEngine: 225 includes, 4 root causes | **243/243** |
| 11 | CryEntitySystem, CryMovie, CryScriptSystem, CryAISystem | **290/290** |
| 12 | CryInput: DirectInput replaced by a browser backend | **307/307** |
| 13 | CryNetwork: Winsock mapped to BSD sockets | **325/325** |

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
tools/fix_includes.py           repairs Win32-only include spellings
tools/selfcontain.py            finds each header's missing dependency
tests/test_winbase.cpp          regression tests for the reconstructed shims
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

### 7. Case-sensitivity and Windows-only include spellings

47 sources spelled `#include "stdafx.h"`; the file is `StdAfx.h`. A further 34
includes used backslash separators (`"XML\\Xml.h"`, `"expat\\expat.h"`) or the
wrong case (`<CrySound.h>` for `crysound.h`, `<winbase.h>` for `WinBase.h`).
All are invisible on NTFS and fatal on ext4 — and in Emscripten's filesystems.

`tools/fix_includes.py` resolves every include against the real files on disk
and corrects it, preserving directory structure and `..` components.

### 8. Headers that were never self-contained

Around 40 headers named types while including nothing, relying on the
`.vcproj`'s fixed compile order. `tools/selfcontain.py` finds each one's real
dependency by trying candidates and keeping only what compiles clean.

It verifies twice, which turned out to matter. Adding `IRenderer.h` to
`ILog.h` made *that* header compile — and silently broke `Tarray.h` and
`Cry_XOptimise.h`, which include it. The tool now re-checks a header's
dependents after every edit and reverts anything that is a net loss.

### 9. `PHYSICS_EXPORTS` was defined platform-wide

`LinuxSpecific.h` defined it unconditionally. It means "I am building
CryPhysics": it selects `dllexport` over `dllimport`, and it makes `Cry_Math.h`
skip its no-op `VALIDATOR_*` stubs on the assumption that
`CryPhysics/utils.h` will supply the real ones.

Defining it for every module meant every translation unit claimed to be
CryPhysics, so the stubs never existed and `physinterface.h` — which uses
`VALIDATORS_START` in six struct bodies — failed to compile in anything that
included it. The `dllexport` half was harmless on Linux, which is why it went
unnoticed.

### 10. The render header cycle

`IShader.h`, `IRenderer.h`, `VertexFormats.h` and `RendElement.h` form a cycle,
and `LeafBuffer.h` needs a *complete* `SMRendTexVert` (it takes its `sizeof`).
Two changes broke it:

- `IRenderer.h` uses `struct_VERTEX_FORMAT_P3F_COL4UB_TEX2F` by pointer only,
  so a forward declaration replaces the include.
- `RendElement.h` declared `SMRendTexVert` and `SVertBufComps` *after* its
  `ColorDefs.h` include, which transitively pulls the whole renderer. Both
  structs depend on nothing (two floats and four bools), so hoisting them above
  that include was the minimal fix.

### 11. More latent bugs

| Bug | Effect |
|---|---|
| `CCryFile::SeekToEnd` declared `size_t`, no `return` | callers read an indeterminate value |
| `class string: public string` (`StlDbgAlloc.h`) | class was its own base; every constructor delegated to itself. Sibling wrappers all say `std::` — the qualifier was simply dropped |
| `operator=(...) : std::allocator<T>(rThat)` | base-initializer on an assignment operator, copy-pasted from the constructor above |
| `&pe_status_nparts()` (2 sites) | taking the address of a temporary |
| `IsAMD64()` `#error not supported here` | the `LINUX64` branch demanded a Win64 SDK macro. It also cannot just return true — the real target is wasm32, which is not AMD64 |
| `EF_Query()` result cast to `int` | returns `void*` carrying a small integer; truncating on 64-bit |

`XmlParser.h` deserves its own note: it declared 17 variables as
`string::iterator` while assigning `char*` to them. MSVC 7.1's
`std::string::iterator` *was* a `char*` typedef, so it compiled; libstdc++
makes it a class. They are now `char*`, which is what they always were — the
sibling accessors `getBufferPos()` and `getLastBufferPos()` already returned
`char*` for the same values.

## What's next

Milestone 1 is complete: CryCommon and CrySystem compile in full under clang
with the LINUX seam, and the build is green.

### Milestone 2 — platform layer

The shims in `WinBase.h` are honest about what they are. Three things still
need *replacing* rather than shimming:

- **Overlapped I/O.** `RefStreamEngine.cpp`, `RefReadStream.cpp` and
  `RefReadStreamProxy.cpp` use `CreateEvent` + `ReadFileEx` +
  `GetOverlappedResult` + alertable `SleepEx` waits. There is no honest shim —
  it is an async-I/O redesign, and on the web it becomes a worker thread or a
  fetch pipeline. They are excluded, not stubbed, precisely so this stays
  visible.
- **Inline x86 assembly.** 21 files contain `__asm`, which is illegal in wasm.
- **`<ext/hash_map>`** → `unordered_map` (currently silenced with
  `-Wno-deprecated`).

**`memcpy` over vtable pointers.** The build emits 255
`-Wdynamic-class-memaccess` warnings, but they are **two** sites in
`IShader.h` repeated once per including translation unit:

```cpp
CCObject::CloneObject(CCObject *srcObj)   // IShader.h:605
    memcpy(this, srcObj, sizeof(*srcObj));

CMatInfo::operator=(const CMatInfo& src)  // IShader.h:2115
    memcpy(this, &src, sizeof(CMatInfo));
```

Both classes are polymorphic, so the copy overwrites the destination's vtable
pointer. In the common case source and destination have the same dynamic type,
the bytes written are identical and it happens to work — which is why it has
survived. But `CloneObject` takes a **base** pointer: hand it a derived object
and it writes the derived vptr into a base object, and the next virtual call
dispatches into the wrong table.

This is left as-is deliberately. The fix is member-wise assignment, which
changes copy semantics in the renderer's hot path, and there is no test
coverage to verify it against yet. It is recorded here rather than patched
blind — it is a strong candidate for a wasm-only crash that looks inexplicable.

`-Wswitch` (254) is unhandled enum cases; mostly benign, worth one audit.

Also outstanding: `BONE_PHYSICS_COMP::nPhysGeom` is an `int` that carries a
`phys_geometry*` (`CryHeaders.h`). It is sound on wasm32, where pointers are
32-bit, and lossy anywhere else. The real fix widens the field, but the struct
is serialised in `.cgf` assets, so it is a file-format change that belongs with
the asset pipeline.

### Milestone 3 — headless build

Stub renderer/sound/video behind interfaces and target a NULL-render build. A
dedicated server in wasm is genuinely reachable and proves out everything
except the renderer.

The engine's `while (!quit)` main loop cannot work on the browser's event loop.
Asyncify is enabled as the short-term answer; restructuring around
`emscripten_set_main_loop` is the real fix.

### Milestone 4 — the renderer

The bulk of the project. `XRenderOGL` is OpenGL 1.x: 74 `glBegin` sites, 44
references to `GL_NV_register_combiners`, WGL context creation, and NVIDIA Cg
shaders whose `cgGL.lib` is **a binary blob with no source**. WebGL2 supports
none of it. Every shader and the whole fixed-function/combiner pipeline needs
rewriting to GLSL ES.

### Replacing subsystems, not shimming them

Two modules crossed the line from "make it compile" into "make it work
differently", and the distinction between them is the useful part.

**CryInput — replaced.** DirectInput 8 is a COM API built on device
enumeration, acquisition and exclusive cooperative levels. Nothing in a
browser resembles it, so `CryInput/WebInput.cpp` implements `IKeyboard` and
`IMouse` from DOM events instead and `XKeyboard.cpp`/`XMouse.cpp` are not
built. Everything above the device layer is untouched.

Three decisions worth knowing:

- Keys map from `KeyboardEvent.code`, the **physical** key, not `.key`. W-A-S-D
  is a position, not a set of letters; with `.key` an AZERTY player's movement
  keys would silently rebind.
- Events are queued and drained at the frame boundary. The engine polls
  `KeyPressed()` once per frame and expects one true per press — applying
  events on arrival breaks that, which the tests caught.
- Mouse capture is Pointer Lock, which unlike DirectInput can only be
  requested from inside a user-gesture handler. `SetExclusive()` therefore
  *requests* capture; `IsPointerLocked()` is the truth.

Where Win32 behaviour cannot be reproduced, the code says so rather than
faking it: `WaitForKey()` cannot block on the browser's event loop, keyboard
`SetExclusive` has no equivalent, and `XKEY2ASCII` is explicitly US-layout
because the real layout is not exposed.

**CryNetwork — shimmed, and that is honest here.** Winsock is BSD sockets with
different spellings, so `CryCommon/WinSockCompat.h` is a real mapping, not a
fiction. It makes the module compile *and work natively*.

It does **not** make it work in a browser, and no shim can. Far Cry's netcode
is UDP throughout; a browser cannot open a UDP socket at all. Emscripten
emulates BSD sockets over WebSockets, which gets it running but changes the
delivery contract:

| | UDP | WebSocket |
|---|---|---|
| Reliability | may drop | never drops — harmless, retransmit logic just idles |
| Framing | messages | messages — harmless |
| **Ordering** | unordered | **ordered — head-of-line blocking** |

Ordering is the one that hurts. UDP delivers packet N+1 when N is lost and the
game skips the gap; an ordered transport holds N+1 until N is retransmitted, so
a protocol designed to degrade gracefully freezes instead — and the worse the
connection, the worse the mismatch. A WebSocket also needs a relay to reach a
UDP-speaking server, which this repository does not provide.

`CryNetwork/WebTransport.h` is the seam for fixing that properly: a WebRTC
DataChannel in `{ordered:false, maxRetransmits:0}` mode is genuinely
datagram-like and is the correct destination. It costs a signalling server.

### Remaining modules

Four left. The tooling generalises:

```bash
tools/fix_includes.py CryCommon CryAnimation
tools/triage.py --module CryAnimation
tools/selfcontain.py --module CryAnimation --apply
```

| Module | LOC | Note |
|---|---|---|
| CryPhysics | 32k | self-contained, heavy inline x86 asm |
| CryAnimation | 35k | large, but no external SDK |
| CryFont | 50k | bundles FreeType2 |
| CrySoundSystem | 10k | thin wrapper over the missing FMOD 3.61 binary |
| RenderDll | 253k | Milestone 4 |

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
