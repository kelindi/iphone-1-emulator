"""Loopback-only HTTP harness for the original Weather demo protocol."""
import argparse
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import urlsplit

from .weather import DATASET, MAX_REQUEST_BYTES, RequestError, respond


class Handler(BaseHTTPRequestHandler):
    def setup(self):
        super().setup()
        self.connection.settimeout(5)

    def log_message(self, format, *args):
        # Original request URLs can contain device identifiers. This local
        # fixture harness does not need request-body or URL logs.
        pass

    def _reply(self, status, body, content_type="text/plain; charset=utf-8"):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-IPhone-Demo", DATASET)
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)
        self.close_connection = True

    def do_POST(self):
        try:
            path = urlsplit(self.path).path
        except ValueError:
            return self._reply(400, b"Malformed demo URL\n")
        if path != "/dgw":
            return self._reply(404, b"Unknown demo endpoint\n")
        lengths = self.headers.get_all("Content-Length", [])
        if self.headers.get("Transfer-Encoding") is not None or len(lengths) != 1:
            return self._reply(400, b"Expected one Content-Length\n")
        if self.headers.get_content_type() != "text/xml":
            return self._reply(415, b"Expected text/xml\n")
        try:
            if not lengths[0].isascii() or not lengths[0].isdecimal():
                raise ValueError()
            length = int(lengths[0])
            if not 0 < length <= MAX_REQUEST_BYTES:
                return self._reply(413, b"Demo request size limit\n")
            body = self.rfile.read(length)
            if len(body) != length:
                raise RequestError("Incomplete request")
            response = respond(body)
        except (RequestError, ValueError):
            return self._reply(400, b"Malformed or unsupported demo request\n")
        except TimeoutError:
            return self._reply(408, b"Incomplete demo request\n")
        self._reply(200, response, "text/xml; charset=utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8180)
    args = parser.parse_args()
    with HTTPServer(("127.0.0.1", args.port), Handler) as server:
        print("Weather DEMO fixture: http://127.0.0.1:%d/dgw (guest not connected)" % server.server_port, flush=True)
        server.serve_forever()


if __name__ == "__main__":
    main()
