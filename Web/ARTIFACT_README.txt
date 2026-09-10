CryEngine 1.33 web port -- browser build
========================================

Three files: CryWeb.html, CryWeb.js, CryWeb.wasm. Keep them together.

DO NOT DOUBLE-CLICK CryWeb.html
-------------------------------

It will fail, with a message that does not obviously say why:

    Cross-Origin Request Blocked: ... CryWeb.wasm (Reason: CORS request not http)
    failed to asynchronously prepare wasm: both async and sync fetching failed

A page opened from your filesystem has an opaque origin, so the browser refuses
the fetch() that loads the .wasm. Nothing is wrong with the build. WebAssembly
has to be served over HTTP -- this is true of every Emscripten build, not just
this one.

RUN IT
------

From this folder:

    python serve_web.py

then open   http://localhost:8000/CryWeb.html

(On macOS and Linux the interpreter is usually "python3".)

Anything that serves static files works just as well:

    python -m http.server 8000            any Python 3
    npx serve                             if you have Node

WHAT YOU SHOULD SEE
-------------------

A canvas, cleared to a colour, with quads drawn through the engine's own
renderer API. No world, no sound, no game data -- see WEBPORT.md for what is
and is not implemented.

The console output is the engine's log. Startup messages there are expected.

WHICH BROWSER
-------------

Any current Chrome, Edge, Firefox or Safari. The build uses WebGL2, which all
of them have had for years. The WebGPU backend is in the source tree but is not
what this build links.
