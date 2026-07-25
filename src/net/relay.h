#pragma once

#include <expected>
#include <string>

#include "core/error.h"
#include "core/usage.h"

namespace cusage {

// Normalizes a configured relay URL to a base by stripping a legacy "/usage"
// suffix and any trailing slashes, so a fresh path can be appended. Tolerates
// relay_url given either as the bare base (preferred) or as the older full
// ".../usage" publish URL — the existing config on some machines carries the
// latter (see PLAN.md §6.A).
std::string relay_base(std::string url);

// Publishes the usage snapshot (as /api/oauth/usage-shaped JSON) to a
// self-hosted relay so the phone can read it without an Anthropic token. Only
// utilization percentages and reset times are sent — never the OAuth token.
// `token`, when non-empty, is sent as an Authorization: Bearer header.
std::expected<void, Error> publish_usage(const std::string& url,
                                         const std::string& token,
                                         const UsageSnapshot& snapshot);

}  // namespace cusage
