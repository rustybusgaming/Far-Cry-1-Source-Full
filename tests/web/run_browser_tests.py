#!/usr/bin/env python3
"""Run a wasm test page in headless Chromium and report its verdict.

WHY THIS EXISTS

The WebGL2 context is the one piece of the renderer that cannot be tested
without a browser: whether a context is granted depends on the page, the GPU,
the driver blocklist and the browser's own policy, none of which is visible
from the source. So the test is compiled to a page and actually loaded.

Two details are not incidental:

  * wasm cannot be fetched from a file:// URL -- the fetch is cross-origin and
    is refused -- so the build directory is served over HTTP.

  * Headless Chromium has no GPU, and its default software path does not
    expose WebGL2. SwiftShader does, and is what --use-gl=swiftshader selects.
    That makes this a test of the code rather than of the machine's driver,
    which is what we want from CI; it does mean a real driver bug on a user's
    machine is out of scope here.

The page publishes its result to window.__cryTestDone / __cryTestFailures; this
polls for that rather than scraping console output, so a crash before main()
is a timeout rather than a silent pass.
"""

import glob
import http.server
import os
import socketserver
import sys
import threading

def serve(directory):
    handler = lambda *a, **kw: http.server.SimpleHTTPRequestHandler(
        *a, directory=directory, **kw)
    httpd = socketserver.TCPServer(("127.0.0.1", 0), handler)
    httpd.allow_reuse_address = True
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    return httpd, httpd.server_address[1]

def main():
    if len(sys.argv) < 2:
        print("usage: run_browser_tests.py <page.html> [--frames N]",
              file=sys.stderr)
        return 2

    # Two kinds of page.
    #
    # A self-verifying test page runs its own checks and publishes
    # window.__cryTestDone / __cryTestFailures; we wait for the verdict.
    #
    # The engine host has no verdict to publish -- it just runs -- so
    # --frames N instead waits for it to have rendered N frames and checks it
    # got there without a page error. That is the thing worth asserting about
    # a frame loop in a browser: not that it ran, but that it kept YIELDING,
    # since a loop that never returns to the event loop would hang here rather
    # than count.
    want_frames = 0
    if "--frames" in sys.argv:
        want_frames = int(sys.argv[sys.argv.index("--frames") + 1])

    # Optional pixel assertions, as R,G,B. The host samples the framebuffer
    # through IRenderer::ReadFrameBuffer and publishes the values; checking
    # them here is what turns "the loop ran" into "the engine drew the right
    # thing". A centre and a corner together also prove the geometry landed
    # somewhere rather than covering everything.
    def rgb_arg(flag):
        if flag not in sys.argv:
            return None
        return [int(v) for v in sys.argv[sys.argv.index(flag) + 1].split(",")]

    want_centre = rgb_arg("--expect-centre")
    want_corner = rgb_arg("--expect-corner")
    want_tex    = rgb_arg("--expect-textured")
    want_static = rgb_arg("--expect-static")

    page = os.path.abspath(sys.argv[1])
    directory = os.path.dirname(page)
    name = os.path.basename(page)

    try:
        from playwright.sync_api import sync_playwright
    except ImportError:
        print("SKIP: playwright is not installed", file=sys.stderr)
        return 0

    httpd, port = serve(directory)
    url = "http://127.0.0.1:%d/%s" % (port, name)
    print("serving %s at %s" % (directory, url))

    failures = None
    logs = []

    # The container ships a Chromium under /opt/pw-browsers that will not
    # necessarily match the build this Playwright release expects to download.
    # Point at whatever is actually here rather than fetching another copy --
    # the environment sets PLAYWRIGHT_SKIP_BROWSER_DOWNLOAD for the same reason.
    exe = None
    for candidate in (
        "/opt/pw-browsers/chromium/chrome-linux/chrome",
        "/opt/pw-browsers/chromium-1194/chrome-linux/chrome",
    ):
        if os.path.exists(candidate):
            exe = candidate
            break
    for root in sorted(glob.glob("/opt/pw-browsers/chromium-*"), reverse=True):
        if exe:
            break
        candidate = os.path.join(root, "chrome-linux", "chrome")
        if os.path.exists(candidate):
            exe = candidate

    if exe is None:
        print("SKIP: no Chromium found under /opt/pw-browsers", file=sys.stderr)
        httpd.shutdown()
        return 0
    print("chromium: %s" % exe)

    with sync_playwright() as p:
        browser = p.chromium.launch(executable_path=exe, args=[
            # Headless Chromium exposes WebGL2 only through SwiftShader.
            "--use-gl=swiftshader",
            "--enable-unsafe-swiftshader",
            "--disable-gpu-sandbox",
        ])
        pg = browser.new_page()
        pg.on("console", lambda m: logs.append(m.text))
        pg.on("pageerror", lambda e: logs.append("PAGEERROR: %s" % e))

        pg.goto(url, wait_until="load")
        try:
            if want_frames:
                pg.wait_for_function("window.__cryStarted === true", timeout=120000)
                if not pg.evaluate("window.__cryRenderer"):
                    print("the engine started without a renderer", file=sys.stderr)
                    failures = 1
                else:
                    pg.wait_for_function(
                        "window.__cryFrame >= %d" % want_frames, timeout=120000)
                    frames = pg.evaluate("window.__cryFrame")
                    print("rendered %d frames" % frames)
                    failures = 0

                    for name, want, prefix in (("centre", want_centre, "__cryCentre"),
                                               ("corner", want_corner, "__cryCorner"),
                                               ("texture", want_tex, "__cryTex"),
                                               ("static", want_static, "__cryStatic")):
                        if want is None:
                            continue
                        pg.wait_for_function(
                            "window.%sR !== undefined" % prefix, timeout=60000)
                        got = [pg.evaluate("window.%s%s" % (prefix, c))
                               for c in ("R", "G", "B")]
                        # One step of tolerance for float-to-unorm rounding.
                        ok = all(abs(a - b) <= 1 for a, b in zip(got, want))
                        print("%-6s pixel %s (wanted %s) %s"
                              % (name, got, want, "ok" if ok else "MISMATCH"))
                        if not ok:
                            failures += 1
            else:
                pg.wait_for_function("window.__cryTestDone === true", timeout=120000)
                failures = pg.evaluate("window.__cryTestFailures")
        except Exception as exc:
            print("the page never reported a result: %s" % exc, file=sys.stderr)
        browser.close()

    # A thrown exception inside the frame callback stops rendering without
    # necessarily stopping the page, so an otherwise-passing run that logged
    # one is still a failure.
    if failures == 0 and any(l.startswith("PAGEERROR:") for l in logs):
        print("a page error was raised", file=sys.stderr)
        failures = 1

    httpd.shutdown()

    for line in logs:
        print(line)

    if failures is None:
        print("RESULT: no verdict from the page", file=sys.stderr)
        return 1
    if failures:
        print("RESULT: %d failure(s)" % failures, file=sys.stderr)
        return 1
    print("RESULT: all browser tests passed")
    return 0

if __name__ == "__main__":
    sys.exit(main())
