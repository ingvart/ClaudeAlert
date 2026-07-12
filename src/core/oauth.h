#pragma once

#include <expected>

#include "core/credentials.h"
#include "core/error.h"

namespace cusage {

// Exchanges the refresh token for a fresh access token against the official
// token endpoint. PURE: it does not write any file. The server may rotate the
// refresh token, so persisting the result is the caller's decision — and the
// caller must NOT clobber Claude Code's own ~/.claude/.credentials.json, or it
// will desync Claude Code's login.
std::expected<OAuthTokens, Error> refresh_oauth_token(
    const OAuthTokens& current);

// True when the access token is missing an expiry, or expires within
// `skew_seconds` of now (epoch-ms comparison). Unknown expiry is treated as
// "assume still valid" (returns false) since the source omits it rarely.
bool token_expiring_soon(const OAuthTokens& tokens,
                         long long skew_seconds = 300);

}  // namespace cusage
