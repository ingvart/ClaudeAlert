#include <catch2/catch_test_macros.hpp>

#include "core/config.h"

using namespace cusage;

TEST_CASE("parse_config overrides only the keys present", "[config]") {
  const auto config = parse_config(
      "# comment\npoll_seconds = 30\nnotify_on_drop = false\n"
      "weekly_threshold = 75\n");
  CHECK(config.poll_seconds == 30);
  CHECK_FALSE(config.notify_on_drop);
  CHECK(config.weekly_threshold == 75);
  CHECK(config.five_hour_drop_floor == 95);  // Default retained.
}

TEST_CASE("parse_config floors poll_seconds", "[config]") {
  CHECK(parse_config("poll_seconds = 1").poll_seconds == 5);
}

TEST_CASE("serialize_config round-trips", "[config]") {
  NotifyConfig config;
  config.poll_seconds = 45;
  config.weekly_threshold = 70;
  config.five_hour_drop_floor = 98;

  const auto restored = parse_config(serialize_config(config));
  CHECK(restored.poll_seconds == 45);
  CHECK(restored.weekly_threshold == 70);
  CHECK(restored.five_hour_drop_floor == 98);
}
