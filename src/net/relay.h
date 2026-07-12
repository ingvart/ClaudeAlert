#pragma once

#include <expected>
#include <string>

#include "core/error.h"
#include "core/usage.h"

namespace cusage {

// Publishes the usage snapshot (as /api/oauth/usage-shaped JSON) to a
// self-hosted relay so the phone can read it without an Anthropic token. Only
// utilization percentages and reset times are sent — never the OAuth token.
// `token`, when non-empty, is sent as an Authorization: Bearer header.
std::expected<void, Error> publish_usage(const std::string& url,
                                         const std::string& token,
                                         const UsageSnapshot& snapshot);

}  // namespace cusage
