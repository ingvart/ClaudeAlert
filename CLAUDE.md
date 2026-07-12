# ClaudeUsageMonitor

Cross-platform monitor for a Claude subscription's usage windows — the same data
`/usage` shows in Claude Code: the 5-hour rolling window, the weekly window, and
per-model weekly windows. Targets Windows, Linux, and Android (widget +
notifications).

## The data source (reverse-engineered, undocumented)

There is no officially documented endpoint for subscription usage. The official
client calls:

```
GET https://api.anthropic.com/api/oauth/usage
Headers:
  Authorization: Bearer <accessToken>
  anthropic-beta: oauth-2025-04-20
  Content-Type: application/json
```

Response shape (each window object, or absent when `utilization` is null):

```jsonc
{
  "five_hour":        { "utilization": 0.42, "resets_at": "<ISO-8601>" },
  "seven_day":        { "utilization": 0.81, "resets_at": "<ISO-8601>" },
  "seven_day_sonnet": { "utilization": 0.10, "resets_at": "<ISO-8601>" },
  "extra_usage":      { "is_enabled": true, "monthly_limit": 100, "used_credits": 12 }
}
```

`utilization` is **already a percentage** in `[0, 100]` (e.g. `49.0` means 49%),
despite the field name — do not multiply by 100. Round and clamp for display.

**Caveats:** undocumented, may change without notice; unofficial use of your own
OAuth token. Scoped to reading your own usage on your own logged-in device.

### Tokens

- Desktop reads the OAuth bundle from `~/.claude/.credentials.json` (Windows:
  `%USERPROFILE%\.claude\.credentials.json`) under `claudeAiOauth`
  (`accessToken`, `refreshToken`, `expiresAt`, `subscriptionType`). It re-reads
  each poll and never writes (refreshing would rotate Claude Code's shared token).
- **Android logs in on its own** via the OAuth PKCE flow below and stores its own
  independent token bundle, so the phone needs neither the desktop file nor a
  relay. Each device gets its own login; refreshing one never disturbs another.

### OAuth (login + refresh) — the exact wire contract

The endpoint host and client signature are both load-bearing; getting either
wrong fails in *misleading* ways (see the trap below).

- **Authorize** (browser): `GET https://claude.ai/oauth/authorize` with
  `code=true`, `client_id=9d1c250a-e61b-44d9-88ed-5944d1962f5e`,
  `response_type=code`,
  `redirect_uri=https://console.anthropic.com/oauth/code/callback`,
  `scope=org:create_api_key user:profile user:inference`, S256 `code_challenge`,
  and a random `state`. The result page shows the code as `<code>#<state>`.
- **Token** (exchange + refresh): `POST https://claude.ai/v1/oauth/token`,
  **JSON** body. Exchange: `grant_type=authorization_code`, `client_id`, `code`
  (split off the `#state`), `redirect_uri`, `code_verifier`, `state`. Refresh:
  `grant_type=refresh_token`, `refresh_token`, `client_id`. Response carries
  `access_token`, `refresh_token`, `expires_in` (seconds), `scope`.
- **Required request headers** (the server only accepts the official client's
  signature): `User-Agent: claude-cli/<ver> (external, cli)`,
  `Content-Type: application/json`, `Accept: application/json, text/plain, */*`,
  `Referer: https://claude.ai/`, `Origin: https://claude.ai`.

> **Trap (cost us a day):** the token endpoint is on **`claude.ai`**, not
> `console.anthropic.com` (that host 404s for `/v1/oauth/token`). With a *browser*
> User-Agent, `console.anthropic.com` returns a bogus **429 `rate_limit_error`**
> instead of 404 — which looks exactly like a rate limit and isn't. The default
> Python/OkHttp UA gets a hard Cloudflare ban (error 1010). Only the `claude-cli`
> UA + the headers above reach the real OAuth server. `setup-token` tokens are a
> dead end regardless: they're scope-blocked on the usage endpoint (403).

## Architecture

- **`src/core/`** — UI-agnostic C++ library: credential read/refresh, HTTP call,
  JSON parse, usage model, polling. This is the shared brain; it has no UI deps
  and stays libc++-clean so it could be reused on Android if ever wanted.
- **Desktop (Windows + Linux)** — SDL3 (`SDL_Tray`) + Dear ImGui. Tray icon +
  menu always present; an ImGui popup window (created on demand) draws the bars.
- **Android** — Kotlin + Jetpack Compose + Glance widget + WorkManager polling +
  local notifications. Not C++. Logs in directly (OAuth PKCE above), fetches the
  usage endpoint itself, and refreshes its own token in the background worker —
  fully self-contained, no desktop or relay involved.

### `relay/` (optional, legacy)

`relay/usaged.py` is a self-fetching daemon (reads its own `~/.claude`
credentials, refreshes, serves the phone) and `relay/pi_login.py` does the PKCE
login on a headless box; `relay/relay.py` is the original dumb mailbox the
desktop published to. None are needed now that the phone logs in directly —
kept for the always-on-poller topology if ever wanted.

## Notifications

Config-file driven. The headline alert is a **drop in any window's utilization
between polls** (a reset / capacity freed — "you can use Claude again"). Plus
configurable rising thresholds: 5-hour ≥90%, weekly ≥80%, and reset events.

A 5-hour reset is **suppressed while the weekly window is at 100%** — a freed
5-hour window is useless when the weekly cap is exhausted, so that notification
would just be noise.

## Engineering standard

Follows the house C++ standard (C++23, CMake+Ninja, libc++ floor, `std::expected`
error handling with no exceptions outside boundary adapters and the top-level
backstop, smart pointers, static linking, two-space indent, snake_case
functions, CamelCase types, trailing-underscore members). Project-specific
deltas from that standard:

- No 3D/graphics stack (no GLM, OpenGL, GLAD, stb_image) beyond what SDL3+ImGui
  pull in for the desktop UI.
- Added boundary dependency: **libcurl** for HTTPS, wrapped so app code sees
  `std::expected`, never the C API. TLS via the OS backend (Schannel on Windows,
  system OpenSSL on Linux) — no bundled crypto.
- **The OAuth token is never logged**, at any level — only its presence/expiry.

## Build

```bash
cmake --preset debug
cmake --build build/debug
ctest --test-dir build/debug
```
