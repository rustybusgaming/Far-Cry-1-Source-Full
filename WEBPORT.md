# CryEngine 1.33 / Far Cry — web port

Porting the Far Cry engine to run in a browser (WebAssembly + WebGL2).

This document covers **Milestone 1: get the engine through a non-MSVC
toolchain** — now complete for CryCommon and CrySystem. Nothing here renders a
frame yet; this is the foundation work everything else is blocked behind.

---

## Current state

```
CryCommon headers     126/126   100.0%
CrySystem              45/45    100.0%
Cry3DEngine            73/73    100.0%
CryEntitySystem        12/12    100.0%
CryMovie               23/23    100.0%
CryScriptSystem          9/9    100.0%
CryAISystem              3/3    100.0%
CryInput                 8/8    100.0%
CryNetwork             26/26    100.0%
CryPhysics             34/34    100.0%
CryAnimation           72/72    100.0%
CryFont                11/11    100.0%
CrySoundSystem         13/13    100.0%
RenderDll/Common       81/81    100.0%
RenderDll/XRenderNULL  11/11    100.0%
TOTAL                 547/547
```

Starting point was **1/188**. Every engine module now compiles, **including the
backend-independent renderer and Crytek's null renderer**.

**The engine now links and runs.** `build/Headless/Headless` is a single
executable containing every module above, and it starts CryEngine on Crytek's
null renderer, brings up every subsystem, and shuts down cleanly:

```
Calling CreateSystemInterface...
File System Initialization
Stream Engine Initialization
Script System Initialization      <- Lua 4.1 VM, from this tree
Network initialization
Physics initialization
Renderer initialization
Init Shaders
Console initialization
AI initialization                 <- logs and continues; see below
Entity system initialization
Initializing Animation System
Initializing 3D Engine
Initializing Script Bindings

System interface created.
System released cleanly.
```

`ctest` covers this: `headless_boot` asserts the engine reaches
"System interface created", not merely that it exits 0 — the engine will happily
continue after a subsystem drops out, so exit status alone would not notice.

**And it runs in WebAssembly.** The same target builds with `emcmake` and runs
under Node (and in a browser), producing a byte-for-byte identical boot log
apart from pak paths and the hostname:

```
$ node build-wasm/Headless/Headless.js
...
Initializing Script Bindings

System interface created.
System released cleanly.
```

All four tests pass under wasm as well — CMake runs them through Node
automatically. `Headless.wasm` is 9.2 MB against the native binary's 11.9 MB.

`XRenderNULL` is the one that matters strategically. It is Crytek's own null
renderer — 11 sources, ~2.4k lines, shipped for the dedicated server — and it
implements the full `IRenderer` interface while drawing nothing. It is what
makes a **headless build** possible: the engine can link and run with no GL, no
D3D, no Cg and no shader rewrite. Doing it before touching `XRenderOGL` means
the WebGL2 work starts from a running engine rather than a compiling one.

27 further translation units are **excluded by design**: Win32-only code that
is not a port target, plus the parts of CryAISystem missing from the source
drop entirely. Each carries a reason:

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
| 14 | CryPhysics: `validator.h` reconstructed | 359/359 |
| 15 | CryAnimation, CryFont, CrySoundSystem | 455/455 |
| 16 | RenderDll/Common + XRenderNULL | **547/547** |

---

## Building

```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

Run the engine:

```bash
./build/Headless/Headless
```

Tests:

```bash
cd build && ctest --output-on-failure
```

Census of what does and does not compile:

```bash
tools/triage.py                       # ranked table of remaining blockers
tools/triage.py --show "windows.h missing"   # real diagnostics for one category
tools/triage.py --json out.json
```

WebAssembly:

```bash
source /path/to/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-wasm -G Ninja
cmake --build build-wasm
node build-wasm/Headless/Headless.js
cd build-wasm && ctest          # runs the same four tests through Node
```

Built and tested against Emscripten 6.0.9.

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

### Next: a headless build

Everything except the rasteriser now compiles, so the next milestone is
LINKING, not more compiling — and `XRenderNULL` is what makes that reachable
without any shader work. The remaining pieces are known:

1. **Link a headless executable** against the fourteen libraries with
   `CryRenderNULL` as the renderer. Expect undefined symbols where modules
   reference each other's Win32-only code, and the excluded translation units
   to leave real gaps (the overlapped-I/O streamer above all).
2. **Restructure the main loop.** The engine's `while (!quit)` cannot work on
   the browser's event loop. Asyncify is enabled as the short-term answer;
   `emscripten_set_main_loop` is the real fix.
3. **Replace the overlapped-I/O streamer** (Milestone 2), which is the largest
   remaining hole in a runnable build.

### Milestone 4 — the rasteriser

`XRenderOGL` is 45,733 lines and the only complete GL backend, but it is OpenGL
1.x: 74 `glBegin` immediate-mode sites, 44 references to
`GL_NV_register_combiners`, WGL context creation, and NVIDIA Cg shaders whose
`cgGL.lib` is a binary blob with no source. WebGL2 (GLES 3.0) supports none of
it — no immediate mode, no fixed-function combiners, no Cg.

So this is a rewrite, not a port: every shader and the whole
fixed-function/combiner pipeline has to be re-expressed in GLSL ES. It is the
bulk of the remaining project and the reason the honest estimate is measured in
person-years.

`XRenderD3D8`/`D3D9` and `XRenderPS2` have no path to the web at all.

### Inline assembly

Two of the seven inline-assembly sites outside XRenderOGL are now handled, both
timing code that had to be *recomputed* rather than translated — wasm has no
inline assembly at all, being a stack machine with no register file to name:

- `CrySoundSystem/OGGDecoder.cpp` read the TSC with `rdtsc`.
- `RenderDll/RenderPCH.h` had two cycle counters. Its LINUX branch called
  `rdtscl()`, a Linux **kernel** macro from `<asm/msr.h>` that does not exist
  in userspace, so that branch had never compiled at all.

Both now use `CLOCK_MONOTONIC`, which is also better behaved than the TSC ever
was: no drift with frequency scaling, no jump on thread migration. The unit
becomes nanoseconds instead of cycles, which is invisible to callers because
they only difference two readings.

### Linking: many DLLs into one unit

Getting from "everything compiles" to "everything links" was its own problem,
and a different one. The engine is built as one DLL per module and wires them
together at runtime — `LoadDLL("CryScriptSystem")` then
`GetProcAddress("CreateScriptSystem")`. WebAssembly has no synchronous
`dlopen`, so the whole engine has to become a single link unit.

`CryCommon/StaticModules.h` replaces the lookup with a compiled-in table. The
seam is `CryLibrary.h`, so all thirteen call sites in `SystemInit.cpp` are
untouched: they still call `LoadDLL()` then `CryGetProcAddress()`, and still
handle either returning `NULL`. The table is checked at compile time by
including each module's own public header rather than re-declaring the
factories by hand.

The engine's existing `dlopen` path could not have worked in any case — it
prefixes every library name with `getenv("MODULE_PATH")`, which nothing in the
tree sets, so it constructed a `std::string` from `NULL` and aborted.

**Three libraries were sitting in the tree unbuilt.** The CMake port had never
compiled them, so their symbols came up undefined at the first link:

- **Lua 4.1-alpha** (`CryScriptSystem/LUA`) — 60 symbols. Easy to believe
  missing, because the files are named `lapi.c`, `lvm.c` and so on, not
  `lua*.c`. The version matters: 4.1-alpha was never released as such, it
  became Lua 5.0, and nothing later is API-compatible — `lua_setnativedata`,
  `lua_getluafuncdata`, `lua_newuserdatabox` and `lua_xref` have no Lua 5
  equivalents. Building the bundled copy is the only option that keeps the
  semantics the game's `.lua` assets were written against.
- **FreeType 2** (`CryFont/FreeType2`) — 12 symbols. The module set is taken
  from Crytek's already-edited `ftmodule.h`, because `FT_Init_FreeType`
  registers exactly what that file names.
- **zlib / expat / md5**, done earlier for the same reason.

**Once nothing was undefined, 24 symbols were defined twice** — the opposite
problem, and the one that is genuinely inherent to collapsing the DLLs:

| Duplicate | Resolution |
|---|---|
| `GetISystem()` × 8 | Every module carried a private copy returning the pointer to the one `CSystem` that CrySystem had already created. Collapsed onto CrySystem's; no behaviour change. |
| `g_CpuFlags`, `g_SecondsPerCycle` | Per-DLL caches of `ISystem::GetCPUFlags()` / `GetSecondsPerCycle()`, each filled from the same `ISystem`. The renderer's copies were never written at all under the null backend, so sharing CryAnimation's can only improve on zero. |
| `g_bProfilerEnabled` × 3 | Given to `CrySystem/FrameProfileSystem.cpp`, which owns the profiler. |
| `GetExtension()` × 2 | **Not** merged. `CryPak.cpp` returns the *first* dot, `ResFile.cpp` the *last* — for `terrain.detail.dds`, `.detail.dds` versus `.dds`. Per-DLL linkage had kept two different functions with one name apart. CrySystem's has no callers outside its own file, so it became `static`; both keep their semantics. |
| `CIndexedMesh::~CIndexedMesh()` × 2 | One shared class, destructor defined in both Cry3DEngine and CryAnimation. Kept Cry3DEngine's, which is the more complete of the two — CryAnimation's omits `free(m_pColorSec)`. Nothing in this drop allocates that member, so nothing was leaking. |
| `TAnimTcbTrack<…>` × 18 | Not a DLL problem at all: these are explicit specializations defined in a header, which are **not** implicitly inline, so three of CryMovie's own TUs each emitted a strong definition. MSVC put each in its own COMDAT and folded them. Marked `inline`. |

### The renderer needed one define

`NULL_RENDERER` accounted for 70 undefined symbols on its own. `Common/` is not
a shared library — it is source each backend compiles with its own defines, and
Crytek's `XRenderNULL.vcproj` sets `NULL_RENDERER`. Without it, `Shader.h`
declares `CPShader::mfForName` (defined only in the D3D/GL backends), and
`TexMan.cpp`, `Renderer.cpp` and `CImage.cpp` reference the nvDXT, ATI 3Dc and
Intel JPEG blobs — none of which can exist in a web build. `CryRenderCommon`
and `CryRenderNULL` are now one target whose source list is generated from that
`.vcproj` rather than guessed.

### Two real bugs found while linking

- `LUA/llimits.h`'s `IntPoint()` narrowed a pointer to 32 bits **before**
  hashing it, so on a 64-bit target any two objects sharing a low half
  collided. Now mixed at pointer width and truncated after — bit-identical on
  32-bit.
- `LUA/lmem.c` declared `DumpCallStack()` as plain `extern` while
  `ScriptSystem.cpp` defines it `extern "C"`. It is a real callback out of the
  VM: Lua's allocator calls it to print the script call stack when a request
  fails.

### CryAISystem: registered, but exports nothing

The public source drop contains no `CAISystem`, `AIObject`, `AIPlayer` or
`GoalOp` — neither headers nor implementation. The registry lists the module
anyway, with an empty export table, and the difference is load-bearing: an
*unregistered* module makes `CSystem::LoadDLL` call `Quit()` and kill the
engine, whereas a *registered* one with no entry point takes the tolerant path
`InitAISystem()` already has, logs, and continues. That is why the boot log
above has an AI error in it and still finishes.

### Getting to wasm

Doing native clang first paid off exactly as intended: of 547 translation
units, **only 14 failed to compile for wasm**, and the whole port took one
sitting. The failures fell into two groups.

**wasm32 is a 32-bit target, and the tree only knew two shapes.** The port had
been defining `LINUX64` for wasm, which is wrong in a way that would have caused
silent corruption rather than a build failure: `Linux64Specific.h` types
`DWORD_PTR` as `uint64` while `LONG_PTR` stays `long`, so on wasm32 the two
disagree with each other and with the 4-byte pointers they are meant to hold.
Switching to `LINUX32` fixed that — but `LINUX32` also implied `_CPU_X86`, and
wasm is 32-bit *without* being x86, so that had to be separated too.

That change then exposed a pattern repeated in four places: guards written as
`#if defined(LINUX64)` that actually mean **"`intptr_t` is a distinct type from
`int`"**. The two conditions coincide on x86 (on x86-32 `intptr_t` *is* `int`,
so the extra overload would collide) but come apart on wasm32, where pointers
are four bytes yet `intptr_t` is `long` — same width as `int`, different type.
Each site needed the overload the 64-bit guard was withholding:

| Site | Symptom |
|---|---|
| `IScriptSystem.h` `GetParam(int, INT_PTR&)` | no viable overload for `CryEngineDecalInfo::nPartID` |
| `Cry_Math.h` `iszero(intptr_t)` | ambiguous in CryPhysics' branchless pointer arithmetic |
| `smartptr.h`, `LinuxSpecific.h` `CHandle` | see below |

The `GetParam` guard was three separate copies of the same `#if` that had to
agree — declaration in the interface, declaration in the implementation,
definition — and they didn't. It is now one named macro,
`CRY_SCRIPT_HAS_INT_PTR_PARAM`.

**`NULL` is `0L` here.** Emscripten's headers define `NULL` as `0L` rather than
as `__null`. That makes it a `long`, which converts equally badly to `int` and
to a pointer, so every `smartPtr != NULL` and `handle = NULL` in the engine
became ambiguous. `_smart_ptr` and `CHandle` gained `long` overloads. (The
existing `typeof(__null)` overloads could not be reused: `typeof(__null)` is
`int` on wasm32, which is exactly `CHandle<int,-1>`'s handle type.)

**glibc-isms and one kernel header.** `_finite` was `#define`d to `__finite`, a
glibc *internal* symbol that musl does not have — `isfinite` is the C99 spelling
and works everywhere. `MTSafeAllocator` called `std::_Construct` and
`std::_Destroy`, which are libstdc++ internals with no libc++ equivalent; they
are placement new and an explicit destructor call, now written as such. And in
six files `<io.h>` had been "translated" to `<sys/io.h>` — which is not the
POSIX counterpart of Windows' low-level file header but the **x86 port-I/O**
header declaring `inb`/`outb`. Nothing used those; the right header is
`<unistd.h>`.

Two more worth naming:

- `std::map`'s allocator must have `value_type` `pair<const Key, T>`.
  `CryPak.h` declared `CMTSafeAllocator<pair<string, unsigned>>` — libstdc++
  rebinds internally and never notices, libc++ static-asserts. It only ever
  compiled by luck.
- `PAGESIZE` is a POSIX macro, and Emscripten defines it (as 65536), turning
  `PageBucketAllocator`'s `enum { PAGESIZE = 4096 }` into `enum { 65536 = 4096 }`.

**Two latent bugs a newer compiler found.** `Cry_Matrix.h` called
`SetMatFromVectors34()` and `SetRotationZ34()`; neither exists — the members are
`SetMatFromVectors` and `SetRotationZ`. Both sat in never-instantiated templates,
and clang 18 does not diagnose a member call on the current instantiation at
definition time. Clang 24 does.

**The ASE SDK was excluded by accident.** `CryNetwork/Server.cpp` guards the
All-Seeing Eye server-query calls with
`#if !defined(WIN64) && !defined(LINUX64) && !defined(NOT_USE_ASE_SDK)`, so the
native build skipped them only because it happened to define `LINUX64`. wasm is
`LINUX32`, so three `ASEQuery_*` symbols came up undefined. The exclusion is now
stated as `NOT_USE_ASE_SDK` — the mechanism `ProjectDefines.h` documents — for
the reason it is actually true: there is no source for that library on any
platform.

**Link settings.** `cmake/toolchains/Emscripten.cmake` had been written
speculatively in Milestone 1 and was never actually included by anything. It is
now wired in and calibrated against a real build: C++ exceptions are **on**
(Emscripten disables catching by default, and CryPak reports a missing `.pak` by
throwing — with catching off the first absent pak kills startup instead of
logging), while `-pthread` and `ASYNCIFY` are **off**, because the headless
target uses neither and both are expensive. `wasm-ld` also needs no
`--start-group`: it resolves the whole program's symbol table at once instead of
in one left-to-right pass, so the circular module dependencies that force a
link group on GNU ld are a non-issue there.

---

## The renderer

### What actually stands in the way

`XRenderOGL` is **45,733 lines** (not the 71k quoted in an earlier version of
this document — that was wrong). Counting call sites in its `.cpp` files, with
the declaration table excluded:

| Feature | Call sites | Status in WebGL2 |
|---|---:|---|
| `glVertex*` / `glBegin` immediate mode | 595 / 69 | removed |
| Fixed-function matrix stack | 206 | removed |
| `wgl*` context creation | 205 | Windows-only |
| `glTexEnv*` fixed-function texture env | 90 | removed |
| NV register combiners | 85 | removed |
| `glDrawPixels` / `glRasterPos` / `glBitmap` | 78 | removed |
| Client-side vertex arrays | 70 | removed (VBO + attribs only) |
| `GL_QUADS` / `GL_POLYGON` | 54 | removed |
| ARB/NV assembly programs | 22 | removed |
| ATI fragment shader | 12 | removed |

Essentially every drawing path uses something WebGL2 does not have. This is not
a port; the backend has to be rewritten. What *is* reusable is
`RenderDll/Common` — shader parsing, texture management, render elements, leaf
buffers — which is backend-independent source each backend compiles with its
own defines, exactly as `XRenderNULL` does.

### How the new backend is being built

`IRenderer` has 250 pure virtuals; `CRenderer` in `Common` implements most of
them; a backend fills in about a hundred. `XRenderNULL` is a complete working
implementation of every one in ~2,000 lines that draws nothing.

So `CGLESRenderer` **derives from `CNULLRenderer`** and overrides what it has
implemented for real. Three properties make this worth doing over stubbing a
fresh class:

- The unimplemented tail is not stubs I wrote — it is Crytek's own draw-nothing
  implementation, which is the correct behaviour for an unfinished entry point.
- Progress cannot be faked: what is real is what `CGLESRenderer` overrides, and
  everything else is visibly inherited. "How far along is the renderer" has an
  exact answer at any moment.
- The delta is small enough to read: the override list in `GLESRenderer.h`
  *is* the scope of the backend so far, and nothing else is claimed.

An earlier version of this section said the build **enforced** the split, by
leaving `NULL_System.cpp` out of the target so its thirteen methods would have
no definition and the link would fail until the GLES backend supplied them.
That does not work, and the reason is worth recording: `CNULLRenderer`'s vtable
is emitted in `NULL_Renderer.cpp` — the TU defining its key function — and a
vtable needs *every* one of the class's virtuals defined, whether or not a
derived class overrides them. Leaving the file out breaks the base class, not
just the parts being replaced. So it is compiled, with `CRY_GLES_BACKEND`
removing only its module-level tail (the null backend's engine-interface
globals and its `PackageRenderConstructor`, which the GLES backend must own
instead).

It is a scaffold with a deliberate demolition order. As each subsystem is
written against GLES its methods move from inherited to overridden, and when
the last one moves, the dependency on `XRenderNULL` drops out of the CMake
target.

### What works today

A live WebGL2 context created by the engine's own renderer, with viewport,
buffer clears and frame begin/present. Verified in headless Chromium:

```
[gles] WebGL2 context on '#canvas', 320x240
[gles]   renderer : WebKit WebGL
[gles]   version  : OpenGL ES 3.0 (WebGL 2.0 (OpenGL ES 3.0 Chromium))
[gles]   max tex 8192, cube 16384, units 32, attribs 16, MRT 6, samples 4
[gles]   anisotropic yes, S3TC yes
info: centre pixel = 64,128,191,255
```

That last line is the point: the engine cleared to 0.25/0.50/0.75 and the pixel
read back is exactly 64/128/191. Nothing draws geometry yet.

**S3TC is available**, which matters more than it looks — Far Cry's textures are
DXT1/3/5, so they can be uploaded compressed instead of being decompressed on
the CPU into a 32-bit heap.

`tests/web/run_browser_tests.py` serves the build over HTTP (wasm cannot be
fetched from `file://`) and drives headless Chromium through Playwright. It
links `GLESContext.cpp` itself rather than a copy, so it covers the code that
ships; verified to fail when one channel of the clear colour is changed.

## The browser host

`Web/WebMain.cpp` runs the whole engine in a page, on the WebGL2 backend:

```
Renderer initialization
[gles] WebGL2 context on '#canvas', 800x600
XRenderGLES: WebKit WebGL / OpenGL ES 3.0 (WebGL 2.0 (OpenGL ES 3.0 Chromium))
Init Shaders ... Construct Shader '<Default>'... ok
Input initialization
Input: browser backend (no DirectInput)
Keyboard initialized (browser)      <- WebInput, end to end
Mouse initialized (browser)
Entity system / Animation / 3D Engine / Script Bindings

System interface created. Renderer: present
```

### Why the loop had to be inverted

A page has one thread that also services layout, input and compositing, and —
the part that catches people out — **the drawing buffer is presented when the
task that drew it yields**. There is no `SwapBuffers` to call. A renderer can
issue a perfect frame and the canvas stays blank for as long as the loop holds
the thread. The engine's `while(!quit)` never yields, so nothing would ever
appear.

So one frame becomes one callback, via `emscripten_set_main_loop`, which
compiles to `requestAnimationFrame` — frames are paced by the display and pause
in a background tab, both of which the engine's delta-based timing is fine
with. The alternative, Asyncify, rewrites the whole module so a blocking loop
can yield mid-call; it costs size and speed everywhere to avoid restructuring
one function, and nothing here wants to block.

`web_host_frames` asserts the engine renders 30 frames in Chromium. That is the
thing worth asserting about a browser frame loop: not that it *ran*, but that
it kept **yielding** — a loop that never returned to the event loop would hang
the test rather than count.

### Two engine changes this needed

- `CSystem::OpenRenderLibrary` forced the renderer type to `R_NULL_RENDERER`
  on `LINUX`, unconditionally. That was correct for Crytek: their Linux target
  was a dedicated server with no graphics backend at all. The web build has
  one, so under Emscripten the requested type now stands. Native Linux keeps
  the original behaviour, because the reason for it is still true there.
- `r_Driver` defaulted to `"Direct3D9"`, and the cvar is created *inside*
  `InitRenderer` — after any config file is read — so nothing could override a
  wrong default before it was used. On wasm it defaults to `"OpenGL"`, the one
  backend that exists there.

### Missing assets are no longer fatal

`CSystem::InitFont` refused to initialise without `languages/fonts/default.xml`,
diagnosing it as "you're probably running the executable from the wrong working
folder". That is the right diagnosis for a desktop install and meaningless in a
browser: there is no working folder and no local install, the engine starts
against an empty filesystem, and game data arrives over the network afterwards.
Under `_CRY_WEBPORT` a missing font is now a warning. The engine already takes
this view of missing `.pak` archives, and a font is no more fundamental than
the archives that contain one. The consequence is real and logged: no font
means no console or HUD text.

## The engine draws

`IRenderer::DrawDynVB` now works. That is the engine's generic dynamic-geometry
entry point — CryFont builds text through it, six vertices per glyph, and
CryGame's script renderer uses it for debug geometry — so implementing it lights
up the real path rather than a demo one:

```
XRenderGLES: dynamic-vertex program built
[web] centre pixel 32,128,224  corner 0,0,0
```

The host draws a quad over the middle of the screen in `0x20,0x80,0xE0` and
samples the framebuffer back through `IRenderer::ReadFrameBuffer`. Centre
matches exactly; the corner is the clear. `web_host_frames` asserts both, and
was verified to fail on a wrong channel.

Four things had to be right for that pixel to be right, which is why it is worth
asserting rather than eyeballing:

- **A GLSL ES program exists at all.** GLES has no fixed-function pipeline.
  Where the original backend could enable texturing and call `glBegin`, nothing
  can be drawn here without a compiled program.
- **The colour swizzle.** The engine packs `UCol` as **B,G,R,A** when `gbRgb` is
  false, which every backend in this tree sets — it is the Direct3D byte order,
  and the GL backends asked the driver for `GL_BGRA` to compensate. GLES 3.0 has
  no BGRA vertex format, so the bytes are read in their natural order and
  reordered in the shader. Free there; a per-vertex CPU pass otherwise.
- **Quads.** GLES has no `GL_QUADS` and the engine uses `R_PRIMV_QUADS` freely
  (2D images, sprites). Each quad is expanded to two triangles at draw time,
  which keeps it a backend concern instead of a change every caller has to make.
- **The vertical flip.** Screen space here has y increasing *downward* — the
  engine positions 2D from the top-left, as Windows does — while GL clip space
  has y up. `Set2DMode`'s ortho passes `top=0, bottom=h` to invert it. Get this
  wrong and text renders upside down.

Two smaller notes. The dynamic pool is CPU memory uploaded at draw time rather
than a mapped buffer, because **WebGL has no `glMapBufferRange`** — there is no
pointer into GPU memory to hand across the JavaScript boundary. And the vertex
layout is recorded once in a VAO rather than re-specified per draw, since each
`glVertexAttribPointer` is a separate crossing into JavaScript.

`ReadFrameBuffer` is implemented for real (it is what screenshots use). It reads
`GL_RGBA`/`GL_UNSIGNED_BYTE` — the one combination GLES 3.0 guarantees — and
narrows to RGB itself rather than handing `GL_RGB` to a driver that is permitted
to reject it. Nothing in the frame path calls it: `glReadPixels` is a hard
synchronisation point, and on the web the answer also has to cross back out of
the GPU process.

## Textures

`DownLoadToVideoMemory`, `UpdateTextureInVideoMemory`, `SetTexture` and
`RemoveTexture` now work, so the engine can put pixels on the GPU and sample
them:

```
[web] proof texture id 1
[web] untextured 32,128,224  textured 192,64,16  corner 0,0,0
```

The textured quad's texel is uploaded as `eTF_8888` — the engine's BGRA — as
`{0x10,0x40,0xC0}`, and comes back as `192,64,16`. All three samples are
asserted, and the negative case was checked against `16,64,192`, which is
exactly the failure a missed swizzle would produce.

Three things worth recording:

- **BGRA has to be reordered on the CPU.** `eTF_8888` is, in the engine's own
  header, "usually BGRA". GLES 3.0 can express that with
  `GL_TEXTURE_SWIZZLE_R`/`_B`, but **WebGL2 does not expose texture swizzle**,
  so the reorder happens at upload. Once per texture rather than per frame, and
  skipped entirely for `eTF_RGBA` — which is why that format exists as a
  separate entry, its own comment noting it was "used only in
  `CGLRenderer::DownLoadToVideoMemory`".
- **DXT goes straight through.** Compressed data must *not* be touched;
  DXT1/3/5 upload via `glCompressedTexImage2D`. Since Chromium reports
  `WEBGL_compressed_texture_s3tc` available and Far Cry's textures are almost
  entirely DXT, the common case is a direct upload with no CPU work at all.
- **The min filter is a trap.** A texture with no mip chain whose minification
  filter asks for one is *incomplete* and samples as solid black, with no error
  reported anywhere. The upload path sets `GL_LINEAR` unless it actually
  generated mipmaps. `GL_UNPACK_ALIGNMENT` is set to 1 for the same class of
  reason: the default of 4 silently skews any upload whose row length is not a
  multiple of four.

Mipmaps are generated rather than read: `nummipmap > 1` means the caller wants a
chain, not that it supplied one. Real DXT assets carry their own, which this
path does not read yet.

## Static vertex buffers

`CreateBuffer`, `UpdateBuffer`, `CreateIndexBuffer`, `DrawBuffer` and
`ReleaseBuffer` now work — the path world geometry takes, as distinct from the
dynamic one:

```
[web] static buffer created
[web] untextured 32,128,224  textured 192,64,16  static 96,176,48  corner 0,0,0
```

All four samples are asserted, each in a different colour so no two can be
confused, and the static one was verified to fail when its expected value is
changed.

### Seventeen formats, one binder

The engine has seventeen vertex formats. `XRenderOGL` handles them with a switch
per entry point — each case naming a struct and calling the matching
`glVertexPointer`/`glColorPointer`/`glTexCoordPointer` trio — roughly 200 lines
repeated in several places, and a place to forget a format.

It doesn't need to be written that way, because **the engine already ships the
description**. `CryCommon/VertexFormats.h` has `m_VertexSize[]` giving every
format's stride and `gBufInfoTable[]` giving the byte offset of the colour,
texture coordinate, secondary colour and normal within it. Those two tables are
enough to bind any format generically, so this backend has one binder instead of
seventeen cases, and a format added to the engine works here without a change.

The tables use `0` to mean "this format has no such attribute" — position is
always at offset 0, so a zero colour offset cannot be a real one. That is the
same convention `CLeafBuffer` reads them with.

Two details that would otherwise be silent:

- **A disabled vertex attribute keeps whatever constant was last set for it.**
  Formats without a colour get an explicit white rather than an assumption.
- **`TRP3F` positions are pre-transformed** — `x,y,z,rhw`, already in screen
  space. Running them through a projection would put them somewhere arbitrary.
  The engine only uses that format in 2D, so `DrawBuffer` says so out loud if it
  ever sees one outside 2D mode rather than drawing it wrong.

A VAO is cached per GL buffer, since a static mesh's layout never changes and
each `glVertexAttribPointer` is a separate crossing into JavaScript.

`DrawBuffer` does *not* expand `R_PRIMV_QUADS` the way the dynamic path does:
by then the indices are already in a GL buffer, and rewriting them would mean
reading them back. It logs once and skips.

The engine's indices are 16-bit throughout, capping a buffer at 65536 vertices.
That limit belongs to the data, not to this backend — WebGL2 supports 32-bit
indices, so it is the meshes that would have to change.

### Next

The shader translation: turning Crytek's shader scripts, register combiners and
assembly programs into GLSL ES. That remains the largest single piece of work in
the renderer, and nothing before it draws a world — everything drawn so far goes
through one hand-written program.

---

### Known hard blockers

| Blocker | Status |
|---|---|
| Cg shaders (`cgGL.lib`) | binary only — full shader rewrite required |
| Sound (`crysound.lib`) | binary only; it is **FMOD 3.61** rebranded (`CS_SAMPLE`, `CS_STREAM`, `CS_DSPUNIT`). Must be reimplemented over OpenAL/WebAudio. CrySoundSystem is therefore left out of the link entirely — which costs nothing today, because `CSystem::InitSound` is wrapped in `#if !defined(LINUX)` and is never called on this platform |
| Bink video (`binkw32.dll`) | binary only — cutscenes need re-encoding |
| Networking | browsers cannot open raw sockets; needs a WebSocket/WebRTC relay |
| Game assets | several GB of `.pak`; a streaming problem, and only distributable to people who own the game |

---

## Licensing

This tree is Crytek-copyrighted, **not** open source. The bundled licence
permits free, non-commercial modification and redistribution of the SDK, which
covers a hobby port — but game assets cannot be redistributed, and a commercial
release needs Crytek's agreement.
