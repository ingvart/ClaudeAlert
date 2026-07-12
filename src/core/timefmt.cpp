#include "core/timefmt.h"

#include <charconv>
#include <string>

namespace cusage {

namespace {

// Reads exactly `count` digits starting at `offset` into `out`. Returns false if
// the characters are not all digits or run past the end.
bool read_uint(std::string_view text, std::size_t offset, std::size_t count,
               int& out) {
  if (offset + count > text.size()) return false;
  const std::string_view field = text.substr(offset, count);
  for (const char c : field) {
    if (c < '0' || c > '9') return false;
  }
  const auto [ptr, ec] =
      std::from_chars(field.data(), field.data() + field.size(), out);
  return ec == std::errc();
}

// Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm), valid for
// the Gregorian calendar across the range we care about.
long long days_from_civil(long long year, int month, int day) {
  year -= month <= 2;
  const long long era = (year >= 0 ? year : year - 399) / 400;
  const auto yoe = static_cast<unsigned>(year - era * 400);
  const auto doy = static_cast<unsigned>(
      (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1);
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<long long>(doe) - 719468;
}

}  // namespace

std::optional<long long> iso8601_utc_to_epoch_seconds(std::string_view text) {
  // Expect "YYYY-MM-DDTHH:MM:SS" (19 chars) with the canonical separators.
  if (text.size() < 19) return std::nullopt;
  if (text[4] != '-' || text[7] != '-' || text[13] != ':' || text[16] != ':') {
    return std::nullopt;
  }
  const char sep = text[10];
  if (sep != 'T' && sep != 't' && sep != ' ') return std::nullopt;

  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  if (!read_uint(text, 0, 4, year) || !read_uint(text, 5, 2, month) ||
      !read_uint(text, 8, 2, day) || !read_uint(text, 11, 2, hour) ||
      !read_uint(text, 14, 2, minute) || !read_uint(text, 17, 2, second)) {
    return std::nullopt;
  }
  if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 ||
      minute > 59 || second > 60) {
    return std::nullopt;
  }

  const long long days = days_from_civil(year, month, day);
  return days * 86400 + hour * 3600 + minute * 60 + second;
}

std::string humanize_duration(long long seconds) {
  if (seconds <= 0) return "now";
  const long long days = seconds / 86400;
  const long long hours = (seconds % 86400) / 3600;
  const long long minutes = (seconds % 3600) / 60;

  if (days > 0) return std::to_string(days) + "d " + std::to_string(hours) + "h";
  if (hours > 0) {
    return std::to_string(hours) + "h " + std::to_string(minutes) + "m";
  }
  if (minutes > 0) return std::to_string(minutes) + "m";
  return "<1m";
}

}  // namespace cusage
