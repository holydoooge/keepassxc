#!/usr/bin/env python3
"""Verify the exact HTTP exchanges WebDavClient performs.

The C++ unit test cannot run in the CI environment yet (the binary exits 1 with
no output under the Windows MinGW build), so this reproduces the same request
shapes from the client's source and asserts the server's responses. It does not
replace the unit test; it pins the protocol contract the client codes against:

  PROPFIND Depth: 0   -> 207 with the ETag inside the multistatus XML body
  GET                 -> 200 with an ETag header
  GET If-None-Match   -> 304 when unchanged
  PUT                 -> 201 on create, 204 on replace
  PUT If-Match        -> 412 when the remote revision moved on
  PUT If-None-Match:* -> 412 when the file already exists
  bad credentials     -> 401
  missing file        -> 404

Usage: verify_webdav_protocol.py
Exit status 0 means every exchange behaved as the client expects.
"""

import base64
import re
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
SERVER = HERE / "webdav_test_server.py"
USER = "kpxc-test"
PASSWORD = "test-password"

failures = []
checks = 0


def check(label, condition, detail=""):
    global checks
    checks += 1
    if condition:
        print(f"  OK   {label}")
    else:
        print(f"  FAIL {label} {detail}")
        failures.append(label)


def basic_auth(user=USER, password=PASSWORD):
    token = base64.b64encode(f"{user}:{password}".encode()).decode()
    return {"Authorization": f"Basic {token}"}


def request(url, method="GET", headers=None, body=None):
    req = urllib.request.Request(url, method=method, data=body)
    for key, value in (headers or {}).items():
        req.add_header(key, value)
    try:
        with urllib.request.urlopen(req) as response:
            return response.status, dict(response.headers), response.read()
    except urllib.error.HTTPError as error:
        return error.code, dict(error.headers), error.read()


PROPFIND_BODY = (
    '<?xml version="1.0" encoding="utf-8"?>'
    '<D:propfind xmlns:D="DAV:"><D:prop>'
    "<D:getetag/><D:getcontentlength/><D:resourcetype/>"
    "</D:prop></D:propfind>"
).encode()


def etag_from_body(xml: bytes) -> str:
    """Mirrors WebDavClient's etagFromPropfind(): the validator is in the XML."""
    match = re.search(rb"<[^>]*getetag[^>]*>([^<]+)<", xml)
    return match.group(1).decode().strip() if match else ""


def main() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp) / "webdav"
        root.mkdir()
        (root / "db.kdbx").write_bytes(b"revision-one")

        server = subprocess.Popen(
            [sys.executable, str(SERVER), str(root), "0", "--user", USER, "--password", PASSWORD],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        )
        try:
            port = None
            deadline = time.time() + 15
            while time.time() < deadline:
                line = server.stdout.readline()
                if not line:
                    break
                match = re.search(r"PORT (\d+)", line)
                if match:
                    port = int(match.group(1))
                    break
            if port is None:
                print("FAIL: the test server did not report a port")
                return 1

            url = f"http://127.0.0.1:{port}/db.kdbx"
            missing = f"http://127.0.0.1:{port}/nope.kdbx"

            print("PROPFIND Depth: 0")
            status, headers, body = request(
                url, "PROPFIND", {**basic_auth(), "Depth": "0", "Content-Type": "application/xml; charset=utf-8"}, PROPFIND_BODY
            )
            check("207 multistatus", status == 207, f"got {status}")
            etag = etag_from_body(body)
            check("ETag present in the XML body", bool(etag), f"body={body[:80]!r}")
            check("no ETag header on 207 (client must parse the body)", "ETag" not in headers)

            print("GET")
            status, headers, body = request(url, headers=basic_auth())
            check("200 with the file body", status == 200 and body == b"revision-one", f"got {status}")
            get_etag = headers.get("ETag", "")
            check("ETag header matches the PROPFIND value", get_etag == etag, f"{get_etag} != {etag}")

            print("GET If-None-Match (unchanged)")
            status, _, _ = request(url, headers={**basic_auth(), "If-None-Match": etag})
            check("304 when the revision is unchanged", status == 304, f"got {status}")

            print("PUT If-Match (correct validator)")
            status, _, _ = request(url, "PUT", {**basic_auth(), "If-Match": etag}, b"revision-two")
            check("204 on replace", status == 204, f"got {status}")

            print("PUT If-Match (stale validator)")
            status, _, _ = request(url, "PUT", {**basic_auth(), "If-Match": etag}, b"should-not-land")
            check("412 when the remote revision moved on", status == 412, f"got {status}")
            check("the rejected write did not land", (root / "db.kdbx").read_bytes() == b"revision-two")

            print("PUT If-None-Match: * (create)")
            new_url = f"http://127.0.0.1:{port}/new.kdbx"
            status, _, _ = request(new_url, "PUT", {**basic_auth(), "If-None-Match": "*"}, b"created")
            check("201 on create", status == 201, f"got {status}")
            status, _, _ = request(new_url, "PUT", {**basic_auth(), "If-None-Match": "*"}, b"raced")
            check("412 when the file already exists", status == 412, f"got {status}")

            print("error cases")
            status, _, _ = request(url, headers=basic_auth(password="wrong"))
            check("401 on bad credentials", status == 401, f"got {status}")
            status, _, _ = request(missing, "PROPFIND", {**basic_auth(), "Depth": "0"}, PROPFIND_BODY)
            check("404 for a missing file", status == 404, f"got {status}")
            status, _, _ = request(url, "OPTIONS", basic_auth())
            check("200 on the OPTIONS probe", status == 200, f"got {status}")

            print()
            print(f"{checks - len(failures)}/{checks} protocol checks passed")
            if failures:
                print("failed:", ", ".join(failures))
                return 1
            print("the server contract WebDavClient relies on holds")
            return 0
        finally:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()


if __name__ == "__main__":
    sys.exit(main())
