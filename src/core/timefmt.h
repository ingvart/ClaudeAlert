#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace cusage {

// Parses the leading "YYYY-MM-DDTHH:MM:SS" of an ISO-8601 timestamp and returns
// Unix epoch seconds, treating the time as UTC (the usage API reports +00:00).
// Fractional seconds and the trailing offset are ignored. Returns nullopt if the
// fixed prefix is not well-formed.
std::optional<long long> iso8601_utc_to_epoch_seconds(std::string_view text);

// Renders a positive duration compactly: "2d 3h", "2h 18m", "7m", or "<1m".
// Non-positive input renders as "now".
std::string humanize_duration(long long seconds);

}  // namespace cusage
