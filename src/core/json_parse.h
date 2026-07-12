#pragma once

#include <expected>
#include <string_view>

#include <nlohmann/json.hpp>

#include "core/error.h"
#include "core/usage.h"

namespace cusage {

// Boundary adapter over nlohmann/json, which throws. Converts a parse failure
// into an Error; the parser's detail is folded into the message. Does not log —
// the caller decides the level, since malformed input is an expected error.
std::expected<nlohmann::json, Error> parse_json(std::string_view text);

// Maps a decoded /api/oauth/usage document into a UsageSnapshot. Defensive: a
// missing or null window is simply absent in the result, never an error. Only a
// structurally wrong document (not an object) is an error.
std::expected<UsageSnapshot, Error> parse_usage(const nlohmann::json& doc);

}  // namespace cusage
