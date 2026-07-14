"""Development server for the clock's web pages.

Serves the web/ sources raw -- no gzip, no hash stamping, no reflash -- and
proxies everything else (all /api/* calls) to a real device, so a page edit
is testable with a browser reload against live device data:

    python tools/dev_server.py --device 192.168.4.1 [--port 8080]

Then open http://localhost:8080/ in a browser. Devtools show the readable
sources with real line numbers. Flash the firmware only when the pages are
final; the on-device rendering is identical because the build packages these
same files.
"""

import argparse
import http.server
import pathlib
import sys
import urllib.error
import urllib.request

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from web_manifest import PAGES, SHARED

WEB_DIR = pathlib.Path(__file__).resolve().parent.parent / "web"


class DevHandler(http.server.BaseHTTPRequestHandler):
    device = None  # Set from --device before the server starts.

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path in PAGES:
            self.send_file(WEB_DIR / "pages" / PAGES[path], "text/html")
        elif path in SHARED:
            filename, content_type = SHARED[path]
            self.send_file(WEB_DIR / filename, content_type)
        else:
            self.proxy()

    def do_POST(self):
        self.proxy()

    def do_DELETE(self):
        self.proxy()

    def send_file(self, file, content_type):
        body = file.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def proxy(self):
        url = f"http://{self.device}{self.path}"
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length) if length else None
        request = urllib.request.Request(url, data=body, method=self.command)
        if self.headers.get("Content-Type"):
            request.add_header("Content-Type", self.headers["Content-Type"])
        try:
            with urllib.request.urlopen(request, timeout=20) as response:
                self.relay(response.status, response.headers, response.read())
        except urllib.error.HTTPError as error:
            self.relay(error.code, error.headers, error.read())
        except OSError as error:
            self.send_error(502, f"device {self.device} unreachable: {error}")

    def relay(self, status, headers, payload):
        self.send_response(status)
        self.send_header("Content-Type",
                         headers.get("Content-Type", "application/octet-stream"))
        # The /view page's chunked loader reads the file size from this header.
        if headers.get("X-File-Size"):
            self.send_header("X-File-Size", headers["X-File-Size"])
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, fmt, *args):
        sys.stderr.write(f"{self.command} {self.path} -> {fmt % args}\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--device", required=True,
                        help="IP (or ip:port) of a running clock to proxy /api calls to")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()
    DevHandler.device = args.device
    server = http.server.ThreadingHTTPServer(("", args.port), DevHandler)
    print(f"Serving web/ at http://localhost:{args.port}/ "
          f"(APIs proxied to {args.device}); Ctrl-C stops.")
    server.serve_forever()


if __name__ == "__main__":
    main()
