#!/usr/bin/env python3
"""One-time OAuth login for the relay host (Claude subscription, PKCE).

Gives the always-on box (e.g. a Raspberry Pi) its OWN independent Claude login,
so it can fetch usage and refresh its token without involving any other device.
This is a separate authorization from your desktop's Claude Code — refreshing
one never disturbs the other.

Two steps (the PKCE verifier is stashed between them):

  Step 1:  python3 pi_login.py begin
           -> prints an authorization URL. Open it in any browser, approve,
              and copy the code shown on the result page.

  Step 2:  python3 pi_login.py finish "<pasted code>"
           -> exchanges the code and writes ~/.claude/.credentials.json, which
              usaged.py then reads.
"""

import base64
import hashlib
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

CLIENT_ID = "9d1c250a-e61b-44d9-88ed-5944d1962f5e"
AUTHORIZE_URL = "https://claude.ai/oauth/authorize"
# Subscription token endpoint lives on claude.ai. console.anthropic.com 404s
# (and returns a misleading 429 to browser-style requests).
TOKEN_URL = "https://claude.ai/v1/oauth/token"
REDIRECT_URI = "https://console.anthropic.com/oauth/code/callback"
SCOPE = "org:create_api_key user:profile user:inference"
PKCE_FILE = "/tmp/cusage_pkce.json"
CREDS_PATH = os.path.expanduser("~/.claude/.credentials.json")
# The endpoint only accepts the official client's signature: a browser UA is
# deflected (429), the default Python UA is hard-banned (Cloudflare 1010).
USER_AGENT = "claude-cli/1.0.56 (external, cli)"
TOKEN_HEADERS = {
    "Content-Type": "application/json",
    "User-Agent": USER_AGENT,
    "Accept": "application/json, text/plain, */*",
    "Accept-Language": "en-US,en;q=0.9",
    "Referer": "https://claude.ai/",
    "Origin": "https://claude.ai",
}


def b64url(raw: bytes) -> str:
    return base64.urlsafe_b64encode(raw).rstrip(b"=").decode("ascii")


def begin() -> None:
    verifier = b64url(os.urandom(32))
    state = b64url(os.urandom(32))
    challenge = b64url(hashlib.sha256(verifier.encode("ascii")).digest())
    with open(PKCE_FILE, "w", encoding="utf-8") as handle:
        json.dump({"verifier": verifier, "state": state}, handle)
    params = {
        "code": "true",
        "client_id": CLIENT_ID,
        "response_type": "code",
        "redirect_uri": REDIRECT_URI,
        "scope": SCOPE,
        "code_challenge": challenge,
        "code_challenge_method": "S256",
        "state": state,
    }
    print(AUTHORIZE_URL + "?" + urllib.parse.urlencode(params))


def finish(pasted: str) -> None:
    with open(PKCE_FILE, "r", encoding="utf-8") as handle:
        pkce = json.load(handle)
    # The result page returns "code#state"; keep only the code itself.
    code = pasted.split("#")[0].split("&")[0].strip()
    body = json.dumps({
        "grant_type": "authorization_code",
        "client_id": CLIENT_ID,
        "code": code,
        "redirect_uri": REDIRECT_URI,
        "code_verifier": pkce["verifier"],
        "state": pkce["state"],
    }).encode("utf-8")
    req = urllib.request.Request(TOKEN_URL, data=body, headers=TOKEN_HEADERS, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            data = json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", "replace")
        sys.stderr.write(f"token exchange failed (HTTP {exc.code}): {detail}\n")
        sys.exit(1)

    expires_at = int(time.time() * 1000) + int(data.get("expires_in", 0)) * 1000
    scopes = data.get("scope", SCOPE).split()
    bundle = {
        "claudeAiOauth": {
            "accessToken": data["access_token"],
            "refreshToken": data.get("refresh_token", ""),
            "expiresAt": expires_at,
            "scopes": scopes,
            "subscriptionType": data.get("subscription_type") or "",
        }
    }
    os.makedirs(os.path.dirname(CREDS_PATH), exist_ok=True)
    with open(CREDS_PATH, "w", encoding="utf-8") as handle:
        json.dump(bundle, handle, indent=2)
    os.chmod(CREDS_PATH, 0o600)
    print("OK: wrote " + CREDS_PATH)


if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "begin":
        begin()
    elif len(sys.argv) >= 3 and sys.argv[1] == "finish":
        finish(sys.argv[2])
    else:
        sys.stderr.write('usage: pi_login.py begin | finish "<code>"\n')
        sys.exit(2)
