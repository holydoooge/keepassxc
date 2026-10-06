#!/usr/bin/env python3
"""Minimal WebDAV server for exercising WebDavClient without a real server.

Implements just enough of RFC 4918 for the client's needs:
  OPTIONS   capability/auth probe
  PROPFIND  Depth: 0 metadata, including getetag
  GET       file body, plus ETag
  HEAD      headers only, plus ETag
  PUT       file body, honouring If-Match / If-None-Match with 412

The conditional PUT handling is the point: it makes concurrent writers fail
loudly instead of silently overwriting each other.

Usage: webdav_test_server.py <root-dir> [port] [--user U --password P]
Prints the chosen port on stdout as "PORT <n>" so a test harness can pick it
up when port 0 was requested.
"""

import argparse
import base64
import hashlib
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlparse


def etag_for(data: bytes) -> str:
    return '"' + hashlib.md5(data).hexdigest() + '"'


class WebDavHandler(BaseHTTPRequestHandler):
    server_version = "TestWebDAV/1.0"
    protocol_version = "HTTP/1.1"

    # --- helpers ---------------------------------------------------------
    def _path(self) -> Path:
        rel = unquote(urlparse(self.path).path).lstrip("/")
        return (self.server.root / rel).resolve()

    def _read_body(self) -> bytes:
        length = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(length) if length else b""

    def _send(self, code: int, body: bytes = b"", ctype: str = "text/plain", extra: dict | None = None):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        for key, value in (extra or {}).items():
            self.send_header(key, value)
        self.end_headers()
        if self.command != "HEAD" and body:
            self.wfile.write(body)

    def _check_auth(self) -> bool:
        user = self.server.username
        if not user:
            return True
        header = self.headers.get("Authorization") or ""
        if not header.startswith("Basic "):
            return False
        try:
            decoded = base64.b64decode(header[6:]).decode("utf-8")
        except Exception:
            return False
        return decoded == f"{user}:{self.server.password}"

    def _require_auth(self) -> bool:
        if self._check_auth():
            return True
        self._send(401, b"unauthorized", extra={"WWW-Authenticate": 'Basic realm="test"'})
        return False

    def log_message(self, fmt, *args):  # keep test output readable
        if self.server.verbose:
            sys.stderr.write("webdav-test: " + (fmt % args) + "\n")

    # --- verbs -----------------------------------------------------------
    def do_OPTIONS(self):
        if not self._require_auth():
            return
        self._send(200, b"", extra={"DAV": "1,2", "Allow": "OPTIONS,GET,HEAD,PUT,PROPFIND"})

    def do_PROPFIND(self):
        if not self._require_auth():
            return
        self._read_body()
        path = self._path()
        if not path.is_file():
            self._send(404, b"not found")
            return
        data = path.read_bytes()
        body = (
            '<?xml version="1.0" encoding="utf-8"?>\n'
            '<D:multistatus xmlns:D="DAV:"><D:response><D:href>'
            + self.path
            + "</D:href><D:propstat><D:prop>"
            + f"<D:getetag>{etag_for(data)}</D:getetag>"
            + f"<D:getcontentlength>{len(data)}</D:getcontentlength>"
            + "<D:resourcetype/></D:prop>"
            + "<D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response></D:multistatus>"
        ).encode("utf-8")
        # Deliberately no ETag header here: the client must read it from the XML,
        # matching what real WebDAV servers do.
        self._send(207, body, ctype="application/xml; charset=utf-8")

    def do_HEAD(self):
        if not self._require_auth():
            return
        path = self._path()
        if not path.is_file():
            self._send(404)
            return
        data = path.read_bytes()
        self._send(200, b"", extra={"ETag": etag_for(data)})

    def do_GET(self):
        if not self._require_auth():
            return
        path = self._path()
        if not path.is_file():
            self._send(404, b"not found")
            return
        data = path.read_bytes()
        current = etag_for(data)
        if self.headers.get("If-None-Match") == current:
            self._send(304, b"", extra={"ETag": current})
            return
        self._send(200, data, ctype="application/octet-stream", extra={"ETag": current})

    def do_PUT(self):
        if not self._require_auth():
            return
        path = self._path()
        payload = self._read_body()

        exists = path.is_file()
        current = etag_for(path.read_bytes()) if exists else None

        if_match = self.headers.get("If-Match")
        if_none_match = self.headers.get("If-None-Match")

        if if_none_match == "*" and exists:
            self._send(412, b"precondition failed")
            return
        if if_match is not None and if_match != current:
            self._send(412, b"precondition failed")
            return

        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)
        self._send(201 if not exists else 204, b"", extra={"ETag": etag_for(payload)})


class Server(ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, addr, handler, root: Path, username: str, password: str, verbose: bool):
        super().__init__(addr, handler)
        self.root = root
        self.username = username
        self.password = password
        self.verbose = verbose


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root")
    parser.add_argument("port", nargs="?", type=int, default=0)
    parser.add_argument("--user", default="")
    parser.add_argument("--password", default="")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    root.mkdir(parents=True, exist_ok=True)

    httpd = Server(("127.0.0.1", args.port), WebDavHandler, root, args.user, args.password, args.verbose)
    print(f"PORT {httpd.server_address[1]}", flush=True)

    thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    thread.start()
    try:
        thread.join()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
