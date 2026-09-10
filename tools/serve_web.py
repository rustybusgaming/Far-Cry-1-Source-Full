#!/usr/bin/env python3
"""Serve the browser build over HTTP.

WHY THIS EXISTS

Opening CryWeb.html by double-clicking it does not work, and the error does not
say why in so many words:

    Cross-Origin Request Blocked: ... CryWeb.wasm (Reason: CORS request not http)
    failed to asynchronously prepare wasm: both async and sync fetching failed

A file:// page has an opaque origin, so the browser refuses the fetch() that
loads the .wasm. Nothing is wrong with the build; it simply has to be served.
Any static web server will do -- this one exists so there is a correct answer
in the box, and because it sets the .wasm MIME type that Python's own
http.server does not always know.

USAGE

    python3 tools/serve_web.py                    # serves build-wasm/Web
    python3 tools/serve_web.py path/to/folder     # e.g. an unzipped artifact
    python3 tools/serve_web.py . --port 9000

On Windows the interpreter is usually called "python" rather than "python3".
"""

import argparse
import http.server
import mimetypes
import os
import socketserver
import sys

# Emscripten will fall back to a slower instantiation path if the .wasm arrives
# with the wrong content type, and says so in the console. Python's mimetypes
# database does not carry this mapping on every platform.
mimetypes.add_type("application/wasm", ".wasm")


class Handler(http.server.SimpleHTTPRequestHandler):
	def end_headers(self):
		# The build has no threads and so needs no cross-origin isolation.
		# It does need to not be cached, or an updated .wasm silently does not
		# take effect after a rebuild.
		self.send_header("Cache-Control", "no-store")
		super().end_headers()

	def log_message(self, fmt, *args):
		sys.stderr.write("  %s\n" % (fmt % args))


def main():
	ap = argparse.ArgumentParser(description=__doc__,
	                             formatter_class=argparse.RawDescriptionHelpFormatter)
	ap.add_argument("directory", nargs="?", default=None,
	                help="folder to serve (default: build-wasm/Web, else the "
	                     "current folder if CryWeb.html is in it)")
	ap.add_argument("--port", type=int, default=8000)
	args = ap.parse_args()

	root = args.directory
	if root is None:
		for candidate in ("build-wasm/Web", "."):
			if os.path.isfile(os.path.join(candidate, "CryWeb.html")):
				root = candidate
				break

	if root is None:
		sys.exit("no CryWeb.html found in build-wasm/Web or here; "
		         "pass the folder to serve")

	root = os.path.abspath(root)
	page = os.path.join(root, "CryWeb.html")

	if not os.path.isfile(page):
		sys.exit("no CryWeb.html in %s" % root)

	# Named explicitly rather than left to chance: a missing .wasm next to the
	# page is the other way this fails, and it is worth saying so before the
	# browser does.
	for name in ("CryWeb.js", "CryWeb.wasm"):
		if not os.path.isfile(os.path.join(root, name)):
			sys.exit("%s is missing from %s -- all three files must be "
			         "together" % (name, root))

	os.chdir(root)

	socketserver.TCPServer.allow_reuse_address = True
	with socketserver.TCPServer(("127.0.0.1", args.port), Handler) as httpd:
		print("serving %s" % root)
		print("open  http://localhost:%d/CryWeb.html" % args.port)
		print("stop  Ctrl+C\n")
		try:
			httpd.serve_forever()
		except KeyboardInterrupt:
			print("\nstopped")


if __name__ == "__main__":
	main()
