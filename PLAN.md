# PLAN — Remote session "landed" notifications

A step-by-step plan for adding an **optional** feature to Claude Usage Monitor:
get notified (sound + phone notification) when a Claude Code session running on
one of my dev machines **finishes working / lands** (goes idle awaiting input) —
the event the Claude app shows as "the spinner stopped", but which it never
notifies you about.

This document is the spine we follow, in order. Each phase has concrete files
and an acceptance check. Don't start a phase before the previous phase's check
passes. Cross-machine handoff prompts are in the last section.

---

## 1. Goal and non-goals

**Goal.** When a Claude Code session on my Linux (or Windows) machine finishes a
substantial turn and lands awaiting input, I get a notification on my phone (and
optionally on the desktop), routed through my always-on Raspberry Pi relay
(`192.168.0.10`).

**Non-goals / accepted limitations (decided):**

- Missing **manual stops** (Ctrl+C / Esc) is fine — the `Stop` hook does not fire
  on user interrupt, and if I interrupted it, I'm present anyway.
- Missing **"silent tool stops"** (Claude stalls mid-turn after a tool result and
  emits no text) is acceptable for now. A later **activity-timeout** watcher can
  cover this; being alarmed 5 minutes late beats 30 minutes.
- **No per-session enable/disable** in v1. Notify for *all* sessions, filtered
  only by a duration threshold (see §3). The per-session checkbox is deferred.
- **No real-time phone push (FCM)** in v1. Phone learns via polling; worst-case
  latency when the phone is asleep is Android WorkManager's ~15 min floor.

---

## 2. Core design decisions (with rationale)

### 2.1 Feature-flag, do NOT fork
The only thing specific to me is one value: the relay address. Everything else is
generic. So we keep **one codebase on `main`**; the whole feature stays invisible
unless a relay is configured.

- No `relay_url` in config → no session button, no polling, **the app behaves
  exactly as it does today**. "Works identically if it can't reach the Pi" is thus
  the *default* state, which is the safest possible design.
- A fork would create the very "app becomes personal" problem we want to avoid, and
  saddle us with permanent merge conflicts. Rejected.

### 2.2 Hooks, not "ask Claude to run something"
Claude Code **hooks** are executed by the harness, not by the model, so they fire
deterministically regardless of how the model is behaving or how long the session
has run. This is the reliability property we need. "Ask Claude to run a script when
done" decays over long sessions and is rejected as the primary mechanism.

### 2.3 Duration threshold instead of a per-session checkbox (v1)
`Stop` fires on **every** turn, so "notify for all sessions" would ping on trivial
turns ("yes", "continue"). Instead, the relay pairs `UserPromptSubmit` (turn start)
with `Stop` (turn end) **by shared `prompt_id`** (verified in Phase 0), computes the
turn duration, and **only notifies when the turn ran longer than
`session_notify_min_seconds` (default 60)**. That surfaces exactly "it finished the
substantial thing and is now waiting", with zero per-session UI. Additionally,
**suppress the notification when the `Stop` payload's `background_tasks` is
non-empty** — the session isn't truly idle. The checkbox can be layered on later if
ever wanted.

### 2.4 Fire-and-forget forwarder — a down Pi must never affect my sessions
A `Stop` hook runs *before* the session continues. So the forwarder:
- POSTs with a short (~1 s) timeout,
- **always exits 0**, even on failure (unreachable Pi, bad config, etc.),
- is registered with `"async": true` in `settings.json`.

Extend the "identical if the Pi is down" rule to the hook itself: **sessions behave
exactly as now if the relay is unreachable.**

### 2.5 Self-healing inventory
`SessionEnd` won't always fire (crash, laptop sleep). So every event carries a
timestamp; the relay ages out sessions with no activity for N hours, and the UI
shows "landed 3 m ago" rather than a bare state. The inventory is "last known",
never assumed perfect — which also sidesteps the reliability concern.

### 2.6 LAN-only + shared secret; never forward secrets
The relay holds working-directory paths and a session list. It stays LAN-only and
reuses the existing `relay_token` bearer secret for both forwarder→relay and
widget→relay. The forwarder sends **only** `session_id`, `cwd`, `event`,
`timestamp` — never the transcript, never any OAuth token (consistent with the
project's "never log the token / never log the payload" discipline).

### 2.7 Reuse `cusage_core`, don't reinvent
The new C++ helper links the existing `cusage_core` static lib and reuses
`http_post_json` ([http_client.h](src/net/http_client.h)), `cusage::Error` /
`std::expected` ([error.h](src/core/error.h)), the JSON parser
([json_parse.h](src/core/json_parse.h)), and `load_config`
([config.h](src/core/config.h)). No new dependencies.

---

## 3. Architecture and data flow

```
[dev machine: Linux or Windows]  ── one per machine I work on
  Claude Code hooks (settings.json):
    UserPromptSubmit ─┐
    Stop             ─┤   all run the same binary; payload carries hook_event_name
    SessionStart     ─┤   (SessionStart/End added in the inventory phase)
    SessionEnd       ─┘
        │  stdin = hook JSON {session_id, cwd, hook_event_name, ...}
        ▼
  cusage_hook_notify  (new, tiny, links cusage_core)
        │  reads relay_url + relay_token from ~/.claude/cusage.conf
        │  HTTPS POST /session/event  {session_id, cwd, event, ts}   (~1s timeout, always exit 0)
        ▼
[Raspberry Pi 192.168.0.10]  relay.py (extended)
        • in-memory session inventory (self-healing, aged out)
        • pairs UserPromptSubmit→Stop, computes turn duration
        • marks a landing "notify-worthy" when duration >= threshold
        • GET /sessions -> inventory + unacked landings
        ▲ poll (~15s foreground)                    ▲ poll (WorkManager, phone)
        │                                            │
[Desktop widget: cusage_tray]              [Android widget app]
   • button on usage page -> session list     • polls /sessions
   • sound + OS notification on landing        • fires phone notification on landing
```

**Notification latency.** Foreground desktop poll ≈ 15 s. Phone-asleep ≈ up to
15 min (WorkManager floor). Acceptable per §1. FCM push is a future upgrade if the
latency ever bugs me.

---

## 4. The hook contract

### 4.1 Hooks we use
| Hook | Fires when | Used for |
|------|-----------|----------|
| `UserPromptSubmit` | a turn starts | mark **working**; start duration clock |
| `Stop` | a turn lands (spinner stops) | mark **idle**; compute duration; maybe notify |
| `SessionStart` | session spawns | add to inventory *(inventory phase)* |
| `SessionEnd` | session terminates | remove from inventory *(inventory phase)* |

MVP uses only `UserPromptSubmit` + `Stop`. `SessionStart`/`SessionEnd` are added in
the inventory phase for an accurate session list.

### 4.2 stdin payload (VERIFIED in Phase 0, VS Code extension, 2026-07-20)
Actual `Stop` payload fields observed:
```jsonc
{
  "session_id": "60f0db07-…",
  "transcript_path": "…/<id>.jsonl",
  "cwd": "c:\\…\\ClaudeUsageMonitor",
  "prompt_id": "c9ae9c99-…",          // SAME id on the turn's UserPromptSubmit — pair by this
  "permission_mode": "auto",
  "effort": { "level": "high" },
  "hook_event_name": "Stop",
  "stop_hook_active": false,          // re-entrancy guard
  "last_assistant_message": "…full reply text…",  // transcript content — NOT forwarded by default
  "background_tasks": [],             // if non-empty, session isn't truly idle → suppress notify
  "session_crons": [],
  "session_title": "Claude Widget"    // human-readable — show this in the UI list
}
```
`UserPromptSubmit` payload adds `"prompt": "hello"` and shares the turn's
`prompt_id`; it omits the `Stop`-only fields.

**What the forwarder sends (metadata only):** `session_id`, `cwd`, `session_title`,
`event` (= `hook_event_name`), `prompt_id`, and a locally-stamped `ts`. It
**ignores** `transcript_path` and `last_assistant_message` (transcript content,
never forwarded — see §2.6). No timestamp is in the payload, so the forwarder
stamps `ts` itself. Remote control being on/off is irrelevant — hooks fire
regardless.

### 4.3 settings.json registration (per dev machine)
All four entries run the **same** command; the binary branches on
`hook_event_name` from stdin. `async:true` so the session never waits on us.
```jsonc
{
  "hooks": {
    "Stop":            [{ "matcher": "*", "hooks": [{ "type": "command", "command": "/path/to/cusage_hook_notify", "async": true, "timeout": 5 }] }],
    "UserPromptSubmit":[{ "matcher": "*", "hooks": [{ "type": "command", "command": "/path/to/cusage_hook_notify", "async": true, "timeout": 5 }] }],
    "SessionStart":    [{ "matcher": "*", "hooks": [{ "type": "command", "command": "/path/to/cusage_hook_notify", "async": true, "timeout": 5 }] }],
    "SessionEnd":      [{ "matcher": "*", "hooks": [{ "type": "command", "command": "/path/to/cusage_hook_notify", "async": true, "timeout": 5 }] }]
  }
}
```
Windows uses the same shape with the `.exe` path. Each machine's
`~/.claude/settings.json` is per-machine; that's expected.

### 4.4 ⚠️ Known risk to clear in Phase 0
There is a documented bug where the **`Notification` hook does not fire in the
VS Code extension** (fine in the CLI). Docs don't confirm whether `Stop` is
affected. **Phase 0 exists solely to prove `Stop` fires in my VS Code
environment.** If it doesn't, the fallback is running Claude Code from the
integrated terminal (CLI), where hooks are reliable.

---

## 5. C++ house style for the new programs

This repo already *is* the target state (unlike Farmworks, whose `std::expected`
was only aspirational). Match this repo; borrow the few Farmworks habits noted.

**From this repo (authoritative):**
- C++23, `CMAKE_CXX_EXTENSIONS OFF`, Ninja presets, static linking. New exe is one
  more `add_executable` linking `cusage_core`.
- Errors: `std::expected<T, cusage::Error>`; no exceptions outside boundary
  adapters. Reuse `ErrorCode` from [error.h](src/core/error.h).
- Boundary already done: HTTP via [http_client.h](src/net/http_client.h). Reuse it.
- `namespace cusage`, PascalCase types, snake_case free functions, trailing-`_`
  private members, two-space indent, `#pragma once`, header/source split, `/W4`
  (`-Wall -Wextra -Wpedantic`).
- Verbose "why" comments citing rationale — the existing headers set the bar.

**From Farmworks (borrow lightly, don't overdo it — small project):**
- Daemon loops sleep in short chunks so shutdown is observed promptly — already how
  [poller.h](src/core/poller.h) works with `std::stop_token`; the relay's aging
  timer follows the same spirit.
- Never log secrets/payloads — log the *operation and outcome* only. The forwarder
  logs event name + HTTP status, never the token, cwd contents, or transcript.
- Keep CLI arg parsing minimal/hand-rolled; no heavyweight framework.

**Explicitly NOT adopting from Farmworks:** vendored `add_subdirectory` deps (we use
FetchContent), the hand-rolled wire-framing protocol (we use JSON over HTTP), a
custom error taxonomy (`ErrorCode` is enough), `SDL_CreateThread` (std threads).

---

## 6. Component specifications

### 6.A `cusage_hook_notify` (new C++ executable) — ✅ BUILT & locally verified
- **Location:** [src/hook/main.cpp](src/hook/main.cpp), target in
  [CMakeLists.txt](CMakeLists.txt) linking `cusage_core`.
- **Behavior (as built):**
  1. Read all of stdin (the hook JSON).
  2. Parse `session_id`, `cwd`, `hook_event_name`, `prompt_id`, `session_title`,
     and the size of `background_tasks` via the existing JSON parser.
  3. `load_config()` from `~/.claude/cusage.conf`; if `relay_url` empty → return 0
     (feature off on this machine).
  4. Build `{event, session_id, cwd, prompt_id, session_title, background_tasks, ts}`
     and `http_post_json(<base>/session/event, …, {Authorization: Bearer
     relay_token})` with a **2500 ms** timeout. `<base>` tolerates `relay_url` given
     either as the base or the legacy `…/usage` publish URL (`relay_base()` strips a
     trailing `/usage` and slashes) — needed because the existing config carries
     `http://192.168.0.10:8787/usage`.
  5. **Always `return 0`**, whatever happened (even on exception). Logs one line to
     `~/.claude/cusage_hook_notify.log` only when `CUSAGE_HOOK_DEBUG` is set; never
     logs the token, transcript, or `last_assistant_message`.
- **Timestamp:** `ts` = local epoch **seconds** at send time
  (`std::chrono::system_clock`). Epoch (not ISO-8601) so the relay does plain
  arithmetic for turn duration; both ends of a turn come from the same machine
  clock so duration is skew-free.
- **Cross-platform:** identical code; built with MSVC on Windows, will build with
  the system toolchain on Linux.
- **Verified (Windows, local relay):** long turn → landing; short turn → none;
  `background_tasks` non-empty → suppressed; unreachable relay → exit 0 in ~0.3 s.

### 6.B Pi relay session extension ([relay/relay.py](relay/relay.py)) — ✅ BUILT & locally verified
Extends the existing token-guarded mailbox; `/usage` untouched.
- **`POST /session/event`** — body `{event, session_id, cwd, prompt_id,
  session_title, background_tasks, ts}`. Updates the in-memory inventory:
  - `UserPromptSubmit` / `SessionStart` → state `working`; record turn start
    (keyed by `prompt_id`, with a per-session fallback).
  - `Stop` → state `idle`; `duration = ts - start` (paired by `prompt_id`); if
    `duration >= threshold` **and `background_tasks == 0`**, append a **landing**.
  - `SessionEnd` → state `ended`.
  - Every event refreshes `last_seen`; each write triggers a sweep + persist.
- **`GET /sessions`** — returns `{sessions:[{session_id, cwd, title, state,
  last_seen, last_landed_at, last_duration}], landings:[…recent…], now:<epoch>}`.
  `now` is the relay clock so the widget computes "landed N ago" without skew.
- **Landing dedupe:** client-side by `(session_id, landed_at)` (v1 choice). No ack
  endpoint yet; landings are capped at the most recent 50 and aged out.
- **Aging (self-healing):** lazy sweep on every read/write drops sessions and
  landings older than `SESSION_STALE_HOURS`. SessionEnd is never required.
- **Relay-side env config:** `RELAY_TOKEN`, `SESSION_NOTIFY_MIN_SECONDS`
  (default 60), `SESSION_STALE_HOURS` (default 12).
- Persists inventory to `session_state.json` beside `relay_state.json`
  (best-effort), so a relay restart keeps the list.

### 6.C Desktop widget (`cusage_tray`) changes
- **Config gating:** show the session UI only when `relay_url` is set.
- **Core:** new `session_client` in core — `std::expected<SessionInventory, Error>
  fetch_sessions(relay_url, relay_token)` (GET /sessions, parse). New model type
  `SessionInventory` / `SessionEntry` alongside `usage.h`.
- **Polling:** extend the existing poll loop (or a second lightweight loop at
  `session_poll_seconds`, default 15) to fetch sessions and push into `SharedState`
  ([shared_state.h](src/gui/shared_state.h)) — add a `sessions` field + landing
  queue next to `pending_alerts`.
- **UI:** a **button on the usage page** opens a session-list window: one row per
  session (cwd, working/idle dot, "landed N ago"). On a new unacked landing, play a
  sound and raise an OS notification.

### 6.D Android widget app changes
- Mirror C.D in Kotlin: a `/sessions` poll in the existing WorkManager worker; parse
  into the Kotlin usage-domain types ([UsageDomain.kt](android/app/src/main/java/no/automasjon/claudeusage/UsageDomain.kt));
  fire a local notification (own notification channel) on a new landing.
- Session-list screen reachable from the widget/app (matches the desktop button).
- Gated on a configured relay URL, same as everything else.

---

## 7. Config schema additions (`~/.claude/cusage.conf`)
Add to `NotifyConfig` in [config.h](src/core/config.h) / `parse_config`
/ `serialize_config` (all key=value, unknown keys ignored, defaults on absence):

| Key | Default | Meaning |
|-----|---------|---------|
| `relay_url` | *(existing)* | Base relay URL; **empty disables the whole feature** |
| `relay_token` | *(existing)* | Shared bearer secret |
| `session_poll_seconds` | `15` | Widget → relay poll cadence |
| `session_notify_min_seconds` | `60` | (Widget-side mirror; authoritative copy is relay-side) |

The forwarder reuses `relay_url` + `relay_token` from the same file — no new format.

---

## 8. Build order — the steps we follow, in order

Each step lists files touched and an **acceptance check** that gates the next step.

### Phase 0 — Prove `Stop` fires in my VS Code (GATE) — ✅ PASSED 2026-07-20
> Both `Stop` and `UserPromptSubmit` fire reliably in the VS Code extension;
> payload verified (§4.2), turns pairable by `prompt_id`. No CLI fallback needed.
> Throwaway probe (`~/.claude/cusage_hook_probe.ps1` + the two hooks in
> `~/.claude/settings.json`) still installed — replace with the real forwarder hooks
> in Phase 1.

1. Add a throwaway `Stop` hook to `~/.claude/settings.json` that appends a line to
   a log file (a one-line `.cmd`/`.sh`).
2. Run one Claude Code turn in the VS Code extension; confirm the log grew.
3. Repeat for `UserPromptSubmit`.
- **Check:** both log lines appear. If `Stop` does NOT fire in VS Code → switch my
  workflow to the integrated terminal (CLI) and re-verify there. **Do not proceed
  until a reliable firing environment is confirmed.**
- Remove the throwaway hook afterward.

### Phase 1 — `cusage_hook_notify` end to end (MVP pipe)
Files: [src/hook/main.cpp](src/hook/main.cpp), [CMakeLists.txt](CMakeLists.txt),
[relay/relay.py](relay/relay.py) (`POST /session/event` + `GET /sessions`), plus a
2500 ms timeout knob added to [http_client.h](src/net/http_client.h).

**Step 1 — code + local proof — ✅ DONE 2026-07-21 (on Windows).**
Built `cusage_hook_notify`; ran the new relay locally; verified long turn → landing,
short turn → none, `background_tasks` → suppressed, down-relay → exit 0 in ~0.3 s.

**Step 2 — deploy the relay to the Pi (192.168.0.10).** See §10 prompt. Restart
`relay.py` with `RELAY_TOKEN` + `SESSION_NOTIFY_MIN_SECONDS`; confirm `GET /sessions`
responds. The existing `/usage` behaviour is unchanged.

**Step 3 — build the forwarder on Linux + wire real hooks.** See §10 prompt. Build
`cusage_hook_notify` on the Linux dev machine; replace the Phase-0 probe hooks in
`~/.claude/settings.json` with real `Stop` + `UserPromptSubmit` hooks (`async:true`)
pointing at the built binary.
- **Check:** a real >60 s turn shows a landing on `GET http://192.168.0.10:8787/sessions`;
  a trivial turn shows none; stopping the relay leaves sessions behaving normally.
- Also do the same hook wiring on **Windows** (point at
  `build/debug/cusage_hook_notify.exe`) and remove the Phase-0 probe there.

### Phase 2 — Desktop notification (prove the alert UX where it's easy to iterate)
Files: `src/core/session_client.*` (new), `src/gui/shared_state.h`,
`src/gui/main_gui.cpp` / `tray_app`, config keys (§7).
1. Poll `/sessions`, push into `SharedState`.
2. Button on the usage page → session-list window.
3. Sound + OS notification on a new landing; dedupe by `last_landed_at`.
- **Check:** finishing a long session on the dev machine pops a desktop
  notification + sound; the list shows working/idle correctly; feature is invisible
  when `relay_url` is unset.

### Phase 3 — Inventory accuracy
Files: relay (`SessionStart`/`SessionEnd` handling + aging + persistence), settings
(add those two hooks), desktop list polish.
- **Check:** sessions appear on spawn, disappear on end, and stale ones age out; a
  crash (no `SessionEnd`) doesn't leave a permanent "working" ghost.

### Phase 4 — Android widget
Files: Android worker, notification channel, session-list screen, Kotlin models.
- **Check:** finishing a long session raises a phone notification (foreground within
  seconds; background within the WorkManager interval).

### Phase 5 (deferred / optional) — nice-to-haves
- Per-session enable/disable checkbox (needs the inventory from Phase 3; the toggle
  lives in the widget/relay, the forwarder stays dumb).
- Activity-timeout watcher on the relay to catch "silent tool stops" (§1) — flag a
  session idle if no event for M minutes while last state was `working`.
- FCM real-time phone push.

---

## 9. Testing strategy
- **C++ (Catch2, existing harness):** unit-test the forwarder's payload builder and
  the hook-JSON field extraction (pure functions, no network). Add to
  `cusage_tests` in [CMakeLists.txt](CMakeLists.txt).
- **Relay (Python):** a small script that POSTs a scripted event sequence
  (`UserPromptSubmit` → wait → `Stop`) and asserts a landing appears only past the
  threshold; and that aging drops stale sessions.
- **Manual smoke:** the Phase checks above are the real end-to-end tests.
- **Never** put a real token or transcript into a test fixture.

---

## 10. Cross-machine handoff — ready-to-paste prompts
The code for Phase 1 is written and verified on Windows. These are the next actions,
each runnable in a Claude Code session on the named machine. Do them in order:
**A (Pi)** → **B (Linux dev)** → **C (Windows dev)**.

The relay token is already in `~/.claude/cusage.conf` on the Windows box
(`relay_url = http://192.168.0.10:8787/usage`); reuse the same `RELAY_TOKEN` value on
the Pi so existing usage publishing keeps working.

### A. On the Raspberry Pi (192.168.0.10) — deploy the extended relay
> "Pull the latest `relay/relay.py` onto this Pi (git pull, or copy the file).
> Restart the relay so it serves the new session endpoints, keeping the SAME
> `RELAY_TOKEN` it already uses for `/usage`, e.g.:
> `RELAY_TOKEN=<existing-secret> SESSION_NOTIFY_MIN_SECONDS=60 python3 relay/relay.py 8787`
> (install it as the systemd service / whatever currently runs it). Then confirm:
> `curl -H 'Authorization: Bearer <secret>' http://127.0.0.1:8787/sessions` returns
> `{"sessions": [], "landings": [], "now": …}`, and that `GET /usage` still works.
> Follow PLAN.md §6.B."

### B. On the Linux dev machine — build the forwarder + wire real hooks
> "In the ClaudeUsageMonitor checkout: `git pull`. Build the forwarder:
> `cmake --preset debug && cmake --build build/debug --target cusage_hook_notify`.
> Confirm `~/.claude/cusage.conf` has the right `relay_url`
> (`http://192.168.0.10:8787/usage` or the bare base — the forwarder handles both)
> and `relay_token`. Smoke-test:
> `echo '{\"hook_event_name\":\"Stop\",\"session_id\":\"t1\",\"cwd\":\"/tmp\",\"prompt_id\":\"p\",\"session_title\":\"test\",\"background_tasks\":[]}' | CUSAGE_HOOK_DEBUG=1 ./build/debug/cusage_hook_notify`
> and check `~/.claude/cusage_hook_notify.log` shows `HTTP 200`.
> Then add `Stop` and `UserPromptSubmit` hooks to `~/.claude/settings.json`
> (`async:true`, command = absolute path to the built binary) per PLAN.md §4.3,
> removing any Phase-0 probe. Run one long (>60 s) real turn and one trivial turn,
> then verify with
> `curl -H 'Authorization: Bearer <secret>' http://192.168.0.10:8787/sessions`
> that only the long turn produced a landing. Follow PLAN.md Phase 1 Step 3."

### C. On the Windows dev machine (here) — wire real hooks, drop the probe
> "Replace the Phase-0 probe in `~/.claude/settings.json` with real `Stop` +
> `UserPromptSubmit` hooks (`async:true`) pointing at
> `C:\\…\\ClaudeUsageMonitor\\build\\debug\\cusage_hook_notify.exe`; delete
> `cusage_hook_probe.ps1` and its log. Run a long and a trivial turn and confirm the
> landing via the Pi's `/sessions`. Follow PLAN.md §4.3."

---

## 11. Open questions to confirm before/while building
1. **Phase 0 outcome** — does `Stop` fire in the VS Code extension, or do I move to
   the integrated terminal? (Gates everything.)
2. **Landing dedupe** — client-side by `last_landed_at` (simplest, chosen) vs a
   relay `POST /sessions/ack`. Revisit only if duplicate pings appear.
3. **Threshold value** — start at 60 s; tune after living with it.
4. **Primary consumer order** — plan builds desktop (Phase 2) first as the fast
   iteration surface, then Android (Phase 4). The phone is the ultimate target.
```
