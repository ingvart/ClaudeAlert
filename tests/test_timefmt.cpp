#include <catch2/catch_test_macros.hpp>

#include "core/timefmt.h"

using cusage::humanize_duration;
using cusage::iso8601_utc_to_epoch_seconds;

TEST_CASE("parses the usage API timestamp format", "[time]") {
  // Fractional seconds and the +00:00 offset are ignored.
  const auto epoch =
      iso8601_utc_to_epoch_seconds("2026-06-15T02:29:59.728185+00:00");
  REQUIRE(epoch.has_value());
  // 1970-01-01 plus the civil date, in UTC.
  CHECK(*epoch == 1781490599);
}

TEST_CASE("parses a plain second-resolution timestamp", "[time]") {
  const auto epoch = iso8601_utc_to_epoch_seconds("1970-01-01T00:00:00Z");
  REQUIRE(epoch.has_value());
  CHECK(*epoch == 0);
}

TEST_CASE("rejects malformed timestamps", "[time]") {
  CHECK_FALSE(iso8601_utc_to_epoch_seconds("not-a-time").has_value());
  CHECK_FALSE(iso8601_utc_to_epoch_seconds("2026/06/15T00:00:00").has_value());
  CHECK_FALSE(iso8601_utc_to_epoch_seconds("2026-13-15T00:00:00").has_value());
}

TEST_CASE("humanizes durations compactly", "[time]") {
  CHECK(humanize_duration(2 * 86400 + 3 * 3600) == "2d 3h");
  CHECK(humanize_duration(2 * 3600 + 18 * 60) == "2h 18m");
  CHECK(humanize_duration(7 * 60) == "7m");
  CHECK(humanize_duration(30) == "<1m");
  CHECK(humanize_duration(0) == "now");
  CHECK(humanize_duration(-5) == "now");
}
