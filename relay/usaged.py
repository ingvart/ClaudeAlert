#!/usr/bin/env python3
"""Self-fetching usage daemon for the relay host (e.g. a Raspberry Pi).

Reads the host's OWN Claude OAuth credentials (~/.claude/.credentials.json,
created by pi_login.py), refreshes the access token as it nears expiry, polls
the usage endpoint on an interval, caches the result, and serves it to the
phone on GET /usage. With this running, only this box and the phone need to be
on — no desktop in the loop.

The phone interface is unchanged from relay.py: GET /usage with an optional
`Authorization: Bearer <RELAY_TOKEN>`, returning the usage JSON (or 204 if no
successful poll yet). The OAuth token never leaves this box.

Env:
  RELAY_TOKEN   shared secret the phone sends as Bearer (optional but advised)
  PORT          listen port (default 8787)
  POLL_SECONDS  seconds between usage polls (default 600)
"""

import http.server
import json
import os
import sys
import threading
import time
import urllib.error
import urllib.request

CLIENT_ID = "9d1c250a-e61b-44d9-88ed-5944d1962f5e"
# Subscription token endpoint lives on claude.ai (console.anthropic.com 404s).
TOKEN_URL = "https://claude.ai/v1/oauth/token"
USAGE_URL = "https://api.anthropic.com/api/oauth/usage"
CREDS_PATH = os.path.expanduser("~/.claude/.credentials.json")

RELAY_TOKEN = os.environ.get("RELAY_TOKEN", "")
PORT = int(os.environ.get("PORT", "8787"))
POLL_SECONDS = int(os.environ.get("POLL_SECONDS", "600"))
# The token endpoint only accepts the official client's signature (browser UA is
# deflected with 429, default Python UA is hard-banned with Cloudflare 1010).
USER_AGENT = "claude-cli/1.0.56 (external, cli)"
TOKEN_HEADERS = {
    "Content-Type": "application/json",
    "User-Agent": USER_AGENT,
    "Accept": "application/json, text/plain, */*",
    "Accept-Language": "en-US,en;q=0.9",
    "Referer": "https://claude.ai/",
    "Origin": "https://claude.ai",
}

_lock = threading.Lock()
_latest = ""   # cached usage JSON body (empty until first successful poll)
_last_ok = 0   # epoch seconds of last successful poll


def _load_oauth() -> dict:
    with open(CREDS_PATH, "r", encoding="utf-8") as handle:
        return json.load(handle)["claudeAiOauth"]


def _save_oauth(oauth: dict) -> None:
    with open(CREDS_PATH, "w", encoding="utf-8") as handle:
        json.dump({"claudeAiOauth": oauth}, handle, indent=2)
    os.chmod(CREDS_PATH, 0o600)


def _refresh(oauth: dict) -> dict:
    body = json.dumps({
        "grant_type": "refresh_token",
        "refresh_token": oauth["refreshToken"],
        "client_id": CLIENT_ID,
    }).encode("utf-8")
    req = urllib.request.Request(TOKEN_URL, data=body, headers=TOKEN_HEADERS, method="POST")
    with urllib.request.urlopen(req, timeout=30) as resp:
        data = json.loads(resp.read().decode("utf-8"))
    oauth["accessToken"] = data["access_token"]
    if data.get("refresh_token"):
        oauth["refreshToken"] = data["refresh_token"]
    if data.get("expires_in"):
        oauth["expiresAt"] = int(time.time() * 1000) + int(data["expires_in"]) * 1000
    if data.get("scope"):
        oauth["scopes"] = data["scope"].split()
    _save_oauth(oauth)
    return oauth


def _valid_token() -> str:
    oauth = _load_oauth()
    expires_at = int(oauth.get("expiresAt", 0))
    # Refresh a few minutes early to avoid racing the expiry.
    if expires_at and time.time() * 1000 + 300_000 >= expires_at:
        oauth = _refresh(oauth)
    return oauth["accessToken"]


def _fetch_usage() -> str:
    token = _valid_token()
    req = urllib.request.Request(USAGE_URL, headers={
        "Authorization": f"Bearer {token}",
        "anthropic-beta": "oauth-2025-04-20",
        "Content-Type": "application/json",
        "User-Agent": USER_AGENT,
    })
    with urllib.request.urlopen(req, timeout=30) as resp:
        return resp.read().decode("utf-8")


def _poll_loop() -> None:
    global _latest, _last_ok
    while True:
        try:
            body = _fetch_usage()
            with _lock:
                _latest = body
                _last_ok = int(time.time())
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode("utf-8", "replace")[:200]
            sys.stderr.write(f"poll HTTP {exc.code}: {detail}\n")
            sys.stderr.flush()
        except Exception as exc:  # noqa: BLE001 - never let the loop die
            sys.stderr.write(f"poll error: {exc}\n")
            sys.stderr.flush()
        time.sleep(POLL_SECONDS)


class Handler(http.server.BaseHTTPRequestHandler):
    def _authorized(self) -> bool:
        if not RELAY_TOKEN:
            return True
        return self.headers.get("Authorization", "") == f"Bearer {RELAY_TOKEN}"

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

    def log_message(self, *args) -> None:
        pass  # Quiet.


if __name__ == "__main__":
    threading.Thread(target=_poll_loop, daemon=True).start()
    server = http.server.ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    print(f"usage daemon on :{PORT} "
          f"(auth: {'on' if RELAY_TOKEN else 'off'}, poll {POLL_SECONDS}s)")
    server.serve_forever()
