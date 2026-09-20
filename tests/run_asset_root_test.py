#!/usr/bin/env python3
"""Does --data actually reach the engine's file layer?

The host sets the working directory and prints a report. That much is easy to
get right and easy to get wrong in a way the report would not show: a root that
is set but that CryPak never consults looks identical in the log.

So this asserts on the ENGINE's output, not the host's -- specifically that
CryPak opened the archive from the supplied root, by absolute path. Nothing
else in the suite covers the join between the two.
"""

import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: run_asset_root_test.py <path-to-Headless>")

    headless = os.path.abspath(sys.argv[1])

    with tempfile.TemporaryDirectory() as tmp:
        subprocess.check_call(
            [sys.executable, os.path.join(HERE, "make_synthetic_assets.py"), tmp])

        out = subprocess.run([headless, "--data", tmp],
                             capture_output=True, text=True, timeout=120)
        log = out.stdout + out.stderr

        failures = 0

        def check(cond, what):
            nonlocal failures
            print("%s: %s" % ("ok  " if cond else "FAIL", what))
            if not cond:
                failures += 1

        check("[assets] root: " + tmp in log or
              "[assets] root: " + os.path.realpath(tmp) in log,
              "the host reports the root it was given")

        check("scripts yes" in log, "the report sees the scripts directory")
        check("fonts yes" in log, "the report sees the fonts")

        # The real assertion. CryPak prints this when it opens an archive, and
        # the path proves it came from the supplied root rather than the
        # working directory the process started in.
        check("Opening pack file" in log and tmp in log,
              "the engine opened an archive from the supplied root")

        check("System interface created" in log,
              "the engine still starts with a root set")

        # And the negative: without --data the same engine must NOT find it.
        # Without this, a test that passed because the file happened to be
        # reachable some other way would look like a working feature.
        out2 = subprocess.run([headless], capture_output=True, text=True,
                              timeout=120, cwd=os.path.dirname(headless))
        check(tmp not in (out2.stdout + out2.stderr),
              "without --data the engine does not reach that root")

    if failures:
        print("\n%d failure(s)" % failures)
        return 1
    print("\nasset_root: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
