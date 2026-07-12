# Claude Usage Monitor

Monitors a Claude subscription's usage windows — the same data `/usage` shows in
Claude Code: the **5-hour rolling window**, the **weekly window**, and per-model
weekly windows. Desktop (Windows/Linux, C++) and Android (Kotlin).

> **Status:** the C++ core, CLI, and SDL3/ImGui tray app build and run on Windows
> (MSVC), with the live endpoint verified against a real account. The Android app
> is a complete scaffold to open in Android Studio.

## How it works

There is **no official API** for subscription usage. The monitor calls the same
undocumented endpoint Claude Code's `/usage` uses, with the OAuth token Claude
Code already stores locally:

```
GET https://api.anthropic.com/api/oauth/usage
Authorization: Bearer <accessToken>
anthropic-beta: oauth-2025-04-20
```

Response: `five_hour`, `seven_day`, `seven_day_<model>`, and `extra_usage`, each
window carrying `utilization` (already a **percentage, 0–100**, despite the
name) and `resets_at`.

**Caveats:** the endpoint is undocumented and may change without notice; this is
unofficial use of your own token, scoped to reading your own usage on your own
logged-in device. The monitor **never writes** to Claude Code's credentials file
(refreshing would rotate the shared token and desync Claude Code); it re-reads
the token each poll, so Claude Code's own refreshes are picked up automatically.

## Layout

```
src/core/      UI-agnostic library: credentials, oauth refresh, http, json,
               usage model, config, alert diff, poller, time formatting
src/net/       libcurl boundary (HTTPS)
src/platform/  per-OS path resolution (no #ifdef leaks to callers)
src/cli/       command-line front end
src/gui/       SDL3 + SDL_Tray + Dear ImGui tray app
tests/         Catch2 unit tests (parser, alerts, config, time)
android/       Kotlin app: API, alert logic, WorkManager poller, notifications,
               Glance widget, settings screen
```

## Desktop build

Standard presets:

```bash
cmake --preset debug
cmake --build build/debug
ctest --test-dir build/debug
```

### On this machine (CMake/MSVC are not on PATH)

CMake ships inside Visual Studio and the compiler needs its environment. Build
from a normal shell via the VS environment:

```powershell
$vcvars='C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
$bin='C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin'
cmd /c "`"$vcvars`" && `"$bin\cmake.exe`" --preset debug && `"$bin\cmake.exe`" --build build/debug && `"$bin\ctest.exe`" --test-dir build/debug"
```

Or just open the folder in Visual Studio / VS Code (CMake Tools) and build the
`cusage`, `cusage_tray`, and `cusage_tests` targets. Dependencies (nlohmann/json,
spdlog, libcurl, Catch2, SDL3, Dear ImGui) are fetched automatically and linked
statically; TLS uses the OS backend (Schannel on Windows).

## CLI usage (`cusage`)

```
cusage                 # live: read the local token, fetch and print usage
cusage --watch         # poll on an interval and print alerts to the console
cusage --token         # print the current access token (debugging)
cusage --creds         # report token presence/expiry (no network, no secrets)
cusage --file u.json   # parse a captured usage document offline
cusage --help          # usage
```

### Config

`--watch` writes a default config to `~/.claude/cusage.conf` on first run:

```
poll_seconds = 600           # ~10 minutes between polls
notify_on_drop = true        # alert when a window's % drops (capacity freed)
weekly_threshold = 80        # weekly rising-edge alert %, 0 disables
five_hour_drop_floor = 95    # 5-hour drop alarm only when it was >= this %
```

## Tray app (`cusage_tray`)

The tray **icon itself** is the at-a-glance display: two columns on black
(left = 5-hour, right = weekly). A green bar shows window progress (time elapsed
through the window — "good"); a translucent red bar on top shows usage ("bad"),
so the overlap reads orange and usage outpacing the clock shows red poking above
green. Empty/black means no data yet.

Left-click the tray icon for the menu: a summary line, **Open details** (an ImGui
window with per-window bars + countdowns), and **Exit**. The background poller
fetches every ~10 minutes (`poll_seconds`); the icon and countdowns refresh every
30s from the cached snapshot. On a drop / reset / threshold alert a popup appears
(the "capacity freed" alarm). Alerts are also logged.

## Android

Build/install from Android Studio (open `android/`) or with the Gradle wrapper:
`gradlew assembleDebug`, then
`adb install -r app/build/outputs/apk/debug/app-debug.apk`.

The phone is **fully self-contained** — it logs into Claude itself and fetches
usage directly; no desktop, Pi, or relay is involved.

1. Open the app and tap **1. Open Claude login** — approve in the browser (you're
   already signed into Claude there), copy the code it shows.
2. Paste the code into the app and tap **2. Complete login**. On success it shows
   your current 5-hour / weekly numbers in green. **You only do this once** — the
   app refreshes its own session from then on.
3. Set the weekly % and 5-hour drop-floor %, pick an alert sound, **Save
   settings**, and add the **Claude Usage** home-screen widget (1x1).

The widget shows the **same two-column graphic** as the desktop tray icon and
**tapping it opens the config screen**. A `WorkManager` job fetches usage every
**15 minutes** (Android's floor for periodic work) — refreshing the access token
first if needed — diffs against the last snapshot, and posts a **notification**
(with your chosen sound) on a drop (capacity freed — the headline alert) or a
weekly threshold crossing. Alert config can also be overridden by a text file at
`Android/data/no.automasjon.claudeusage/files/cusage.conf`.

> The OAuth login uses `claude.ai/v1/oauth/token` with the Claude-CLI client
> headers — see CLAUDE.md for the exact contract and the `console.anthropic.com`
> "fake 429" trap.

## Notes

- Alert logic (drop / reset / threshold) is shared in spirit between
  `src/core/alerts.cpp` and `android/.../Alerts.kt` and is unit-tested on the C++
  side.
- The OAuth token is never logged at any level — only its presence/expiry.
