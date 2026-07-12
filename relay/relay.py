#!/usr/bin/env python3
"""Self-hosted relay for Claude Usage Monitor.

Stores the latest usage snapshot POSTed by the desktop and serves it to the
phone. Runs entirely on your own machine (e.g. a Raspberry Pi) — no third party.
Only usage percentages and reset times pass through it; never an OAuth token.

Usage:
    RELAY_TOKEN=your-shared-secret python3 relay.py [port]

Endpoints (both require `Authorization: Bearer <RELAY_TOKEN>` when a token is set):
    POST /usage   body = usage JSON   (desktop publishes here)
    GET  /usage   -> latest usage JSON, or 204 if none yet (phone reads here)
"""

import http.server
import json
import os
import sys
import threading

TOKEN = os.environ.get("RELAY_TOKEN", "")
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8787
STATE_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "relay_state.json")

_lock = threading.Lock()


def _load_latest() -> str:
    try:
        with open(STATE_FILE, "r", encoding="utf-8") as handle:
            return handle.read()
    except OSError:
        return ""


_latest = _load_latest()


class Handler(http.server.BaseHTTPRequestHandler):
    def _authorized(self) -> bool:
        if not TOKEN:
            return True
        return self.headers.get("Authorization", "") == f"Bearer {TOKEN}"

    def _send(self, code: int, body: bytes = b"") -> None:
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if body:
            self.wfile.write(body)

    def do_GET(self) -> None:
        if self.path.split("?")[0] != "/usage":
            return self._send(404)
        if not self._authorized():
            return self._send(401)
        with _lock:
            data = _latest
        if not data:
            return self._send(204)
        self._send(200, data.encode("utf-8"))

    def do_POST(self) -> None:
        if self.path.split("?")[0] != "/usage":
            return self._send(404)
        if not self._authorized():
            return self._send(401)
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length).decode("utf-8") if length else ""
        try:
            json.loads(body)  # Reject anything that isn't valid JSON.
        except ValueError:
            return self._send(400)
        global _latest
        with _lock:
            _latest = body
            try:
                with open(STATE_FILE, "w", encoding="utf-8") as handle:
                    handle.write(body)
            except OSError:
                pass  # In-memory copy still serves the phone.
        self._send(200)

    def log_message(self, *args) -> None:
        pass  # Quiet.


if __name__ == "__main__":
    server = http.server.ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    print(f"Claude usage relay listening on :{PORT} (auth: {'on' if TOKEN else 'off'})")
    server.serve_forever()
