#!/usr/bin/env python3
"""Self-hosted relay for Claude Usage Monitor.

Two independent jobs, both LAN-only and token-guarded:

1. Usage mailbox (original): stores the latest usage snapshot POSTed by the
   desktop and serves it to the phone.

2. Session inventory (see PLAN.md): receives Claude Code hook events forwarded by
   `cusage_hook_notify` from each dev machine, tracks which sessions are working
   vs idle, and exposes a "landing" whenever a session finishes a turn that ran
   longer than a threshold (so the widget can notify "your session is done").

Runs entirely on your own machine (e.g. a Raspberry Pi) — no third party. Only
usage percentages, reset times, session ids, working-directory paths and session
titles pass through it; never an OAuth token and never transcript content.

Usage:
    RELAY_TOKEN=your-shared-secret python3 relay.py [port]

Environment:
    RELAY_TOKEN                 shared bearer secret (auth off if unset)
    SESSION_NOTIFY_MIN_SECONDS  min turn duration to count as a landing (default 0
                                = notify on every stop, any length)
    SESSION_STALE_HOURS         drop sessions/landings older than this (default 12)

Endpoints (all require `Authorization: Bearer <RELAY_TOKEN>` when a token is set):
    POST /usage          body = usage JSON        (desktop publishes here)
    GET  /usage       -> latest usage JSON, or 204 if none yet (phone reads here)
    POST /session/event  body = hook event JSON   (cusage_hook_notify posts here)
    GET  /sessions    -> { sessions:[...], landings:[...], now:<epoch>, rev:<int> }
                         with ?since=<rev>: 204 (empty) if unchanged, else full JSON
"""

import http.server
import json
import os
import sys
import threading
import time
import urllib.parse

TOKEN = os.environ.get("RELAY_TOKEN", "")
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8787
_HERE = os.path.dirname(os.path.abspath(__file__))
STATE_FILE = os.path.join(_HERE, "relay_state.json")
SESSION_STATE_FILE = os.path.join(_HERE, "session_state.json")

# Default 0 => notify on EVERY session stop, of any length. Set the env to a
# positive value only if you later want to filter out short turns.
NOTIFY_MIN_SECONDS = int(os.environ.get("SESSION_NOTIFY_MIN_SECONDS", "0"))
STALE_SECONDS = float(os.environ.get("SESSION_STALE_HOURS", "12")) * 3600.0

_lock = threading.Lock()


# ---- usage mailbox -------------------------------------------------------

def _load_latest() -> str:
    try:
        with open(STATE_FILE, "r", encoding="utf-8") as handle:
            return handle.read()
    except OSError:
        return ""


_latest = _load_latest()


# ---- session inventory ---------------------------------------------------

def _load_sessions():
    try:
        with open(SESSION_STATE_FILE, "r", encoding="utf-8") as handle:
            data = json.load(handle)
        return (data.get("sessions", {}), data.get("landings", []),
                int(data.get("rev", 0)))
    except (OSError, ValueError):
        return {}, [], 0


_sessions, _landings, _rev = _load_sessions()
_turns = {}  # prompt_id -> turn-start ts; transient, rebuilt as events arrive
# `_rev` bumps only when a new landing appears. Clients poll GET /sessions?since=
# <rev>; if it equals the current rev, the relay replies 204 (empty) so an idle
# 1-min poll costs ~nothing. It only sends the full JSON when a landing happened.


def _persist_sessions() -> None:
    try:
        with open(SESSION_STATE_FILE, "w", encoding="utf-8") as handle:
            json.dump({"sessions": _sessions, "landings": _landings,
                       "rev": _rev}, handle)
    except OSError:
        pass  # In-memory copy still serves the widget.


def _sweep(now: float) -> None:
    """Drop sessions and landings with no activity within the stale window.

    Self-healing (PLAN.md §2.5): SessionEnd is not guaranteed to arrive (crash,
    laptop sleep), so we never rely on it to remove a session."""
    stale = [sid for sid, s in _sessions.items()
             if now - s.get("last_seen", 0) > STALE_SECONDS]
    for sid in stale:
        _sessions.pop(sid, None)
    _landings[:] = [l for l in _landings
                    if now - l.get("landed_at", 0) <= STALE_SECONDS]


def _handle_event(evt: dict) -> None:
    global _rev
    sid = evt.get("session_id")
    if not sid:
        return
    ts = evt.get("ts")
    if not isinstance(ts, (int, float)):
        ts = time.time()
    event = evt.get("event", "")

    s = _sessions.setdefault(sid, {"state": "unknown", "cwd": "", "title": ""})
    if evt.get("cwd"):
        s["cwd"] = evt["cwd"]
    if evt.get("session_title"):
        s["title"] = evt["session_title"]
    s["last_seen"] = ts

    pid = evt.get("prompt_id")

    if event in ("UserPromptSubmit", "SessionStart"):
        s["state"] = "working"
        s["turn_started_at"] = ts
        if pid:
            _turns[pid] = ts
    elif event == "Stop":
        s["state"] = "idle"
        started = _turns.pop(pid, None) if pid else None
        if started is None:
            started = s.get("turn_started_at")
        duration = (ts - started) if started is not None else 0
        s["last_landed_at"] = ts
        s["last_duration"] = duration
        background = int(evt.get("background_tasks", 0) or 0)
        # A stop is a landing unless background work is still running (the session
        # isn't truly idle then). NOTIFY_MIN_SECONDS defaults to 0, so every stop
        # counts; raise it only to filter out short turns.
        if duration >= NOTIFY_MIN_SECONDS and background == 0:
            _landings.append({
                "session_id": sid,
                "title": s.get("title", ""),
                "cwd": s.get("cwd", ""),
                "landed_at": ts,
                "duration": duration,
            })
            del _landings[:-50]  # Keep only the most recent 50.
            _rev += 1            # New landing -> clients should fetch.
    elif event == "Notification":
        # Claude paused to ask for input/permission (or went idle) mid-turn — not
        # a turn end, so no duration pairing, but it needs attention now, so
        # notify like a stop. (Claude Code's Notification hook is unreliable in the
        # VS Code extension; this fires wherever the hook does, e.g. the CLI.)
        s["state"] = "idle"
        s["last_landed_at"] = ts
        s["last_duration"] = 0
        if int(evt.get("background_tasks", 0) or 0) == 0:
            _landings.append({
                "session_id": sid,
                "title": s.get("title", ""),
                "cwd": s.get("cwd", ""),
                "landed_at": ts,
                "duration": 0,
            })
            del _landings[:-50]
            _rev += 1            # New attention event -> clients should fetch.
    elif event == "SessionEnd":
        s["state"] = "ended"
        s["ended_at"] = ts

    _sweep(time.time())
    _persist_sessions()


def _sessions_snapshot() -> dict:
    now = time.time()
    _sweep(now)
    sessions = []
    for sid, s in _sessions.items():
        sessions.append({
            "session_id": sid,
            "cwd": s.get("cwd", ""),
            "title": s.get("title", ""),
            "state": s.get("state", "unknown"),
            "last_seen": s.get("last_seen", 0),
            "last_landed_at": s.get("last_landed_at"),
            "last_duration": s.get("last_duration"),
        })
    return {"sessions": sessions, "landings": list(_landings), "now": now,
            "rev": _rev}


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

    def _read_body(self) -> str:
        length = int(self.headers.get("Content-Length", "0"))
        return self.rfile.read(length).decode("utf-8") if length else ""

    def do_GET(self) -> None:
        path = self.path.split("?")[0]
        if not self._authorized():
            return self._send(401)
        if path == "/usage":
            with _lock:
                data = _latest
            if not data:
                return self._send(204)
            return self._send(200, data.encode("utf-8"))
        if path == "/sessions":
            # Conditional poll: ?since=<rev>. If the caller already has the current
            # revision, reply 204 (empty) so an idle 1-min poll costs almost no
            # data; otherwise send the full snapshot (which carries the new rev).
            query = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
            since_raw = query.get("since", [None])[0]
            with _lock:
                if since_raw is not None:
                    try:
                        if int(since_raw) == _rev:
                            return self._send(204)
                    except ValueError:
                        pass  # Bad `since` -> just send the full snapshot.
                snapshot = _sessions_snapshot()
            return self._send(200, json.dumps(snapshot).encode("utf-8"))
        return self._send(404)

    def do_POST(self) -> None:
        path = self.path.split("?")[0]
        if not self._authorized():
            return self._send(401)

        if path == "/usage":
            body = self._read_body()
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
            return self._send(200)

        if path == "/session/event":
            body = self._read_body()
            try:
                evt = json.loads(body)
            except ValueError:
                return self._send(400)
            if not isinstance(evt, dict):
                return self._send(400)
            with _lock:
                _handle_event(evt)
            return self._send(200)

        return self._send(404)

    def log_message(self, *args) -> None:
        pass  # Quiet.


if __name__ == "__main__":
    server = http.server.ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    print(f"Claude usage relay listening on :{PORT} "
          f"(auth: {'on' if TOKEN else 'off'}, "
          f"landing threshold: {NOTIFY_MIN_SECONDS}s)")
    server.serve_forever()
