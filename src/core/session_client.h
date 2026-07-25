#pragma once

#include <expected>
#include <string>

#include <nlohmann/json.hpp>

#include "core/error.h"
#include "core/session.h"

namespace cusage {

// Maps a decoded `/sessions` document into a SessionInventory. Defensive like
// parse_usage: missing/mistyped fields fall back to empty/zero rather than
// erroring, so a relay that adds fields never breaks an older client. Only a
// structurally wrong document (not an object) is an error. Exposed separately
// from fetch_sessions so it can be unit-tested without a network.
std::expected<SessionInventory, Error> parse_sessions(const nlohmann::json& doc);

// GETs `<relay_base>/sessions` (Bearer `relay_token` when non-empty) and parses
// it. `relay_url` may be the bare base or the legacy ".../usage" publish URL —
// relay_base() normalizes both. The caller gates this on a configured relay.
std::expected<SessionInventory, Error> fetch_sessions(
    const std::string& relay_url, const std::string& relay_token);

}  // namespace cusage
