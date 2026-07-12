#pragma once

#include <expected>
#include <string>

#include "core/error.h"
#include "core/usage.h"

namespace cusage {

// Fetches the current usage snapshot from /api/oauth/usage using an OAuth access
// token. Read-only; performs no token refresh and writes no files.
std::expected<UsageSnapshot, Error> fetch_usage(const std::string& access_token);

}  // namespace cusage
