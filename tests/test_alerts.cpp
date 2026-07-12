#include <catch2/catch_test_macros.hpp>

#include "core/alerts.h"
#include "core/usage.h"

using namespace cusage;

namespace {

// Values are percentages (0-100), matching the API's utilization scale.
UsageSnapshot make(double five_hour, double weekly) {
  UsageSnapshot snapshot;
  snapshot.five_hour_ = UsageWindow{five_hour, "t1"};
  snapshot.seven_day_ = UsageWindow{weekly, "w1"};
  return snapshot;
}

}  // namespace

TEST_CASE("no alerts when previous is empty", "[alerts]") {
  const NotifyConfig config;
  CHECK(compute_alerts(UsageSnapshot{}, make(100.0, 100.0), config).empty());
}

TEST_CASE("weekly alarms on any drop", "[alerts]") {
  const NotifyConfig config;
  const auto alerts = compute_alerts(make(50.0, 60.0), make(50.0, 40.0), config);
  REQUIRE(alerts.size() == 1);
  CHECK(alerts.front().kind == AlertKind::window_dropped);
  CHECK(alerts.front().window == "weekly");
}

TEST_CASE("5-hour drop below the floor does not alarm", "[alerts]") {
  const NotifyConfig config;  // five_hour_drop_floor == 95
  CHECK(compute_alerts(make(90.0, 50.0), make(0.0, 50.0), config).empty());
}

TEST_CASE("5-hour drop at/above the floor alarms", "[alerts]") {
  const NotifyConfig config;
  const auto alerts = compute_alerts(make(96.0, 50.0), make(0.0, 50.0), config);
  REQUIRE(alerts.size() == 1);
  CHECK(alerts.front().kind == AlertKind::window_dropped);
  CHECK(alerts.front().window == "5-hour");
}

TEST_CASE("5-hour reset suppressed while weekly is consumed", "[alerts]") {
  const NotifyConfig config;
  // Weekly pinned at 100%: a freed 5-hour window is useless, so no alert.
  CHECK(compute_alerts(make(96.0, 100.0), make(0.0, 100.0), config).empty());
}

TEST_CASE("5-hour reset still alarms when weekly not fully consumed", "[alerts]") {
  const NotifyConfig config;
  const auto alerts = compute_alerts(make(96.0, 99.0), make(0.0, 99.0), config);
  REQUIRE(alerts.size() == 1);
  CHECK(alerts.front().window == "5-hour");
}

TEST_CASE("weekly rising past its threshold crosses", "[alerts]") {
  const NotifyConfig config;  // weekly_threshold == 80
  const auto alerts = compute_alerts(make(50.0, 50.0), make(50.0, 85.0), config);
  REQUIRE(alerts.size() == 1);
  CHECK(alerts.front().kind == AlertKind::threshold_crossed);
  CHECK(alerts.front().window == "weekly");
}

TEST_CASE("5-hour has no rising-threshold alarm", "[alerts]") {
  const NotifyConfig config;
  CHECK(compute_alerts(make(50.0, 50.0), make(99.0, 50.0), config).empty());
}

TEST_CASE("notify_on_drop disabled suppresses drops", "[alerts]") {
  NotifyConfig config;
  config.notify_on_drop = false;
  CHECK(compute_alerts(make(96.0, 80.0), make(0.0, 10.0), config).empty());
}

TEST_CASE("per-model weekly windows alarm on any drop", "[alerts]") {
  const NotifyConfig config;
  UsageSnapshot previous;
  previous.model_weekly_ = {{"sonnet", UsageWindow{100.0, "a"}}};
  UsageSnapshot current;
  current.model_weekly_ = {{"sonnet", UsageWindow{0.0, "b"}}};

  const auto alerts = compute_alerts(previous, current, config);
  REQUIRE(alerts.size() == 1);
  CHECK(alerts.front().window == "weekly:sonnet");
  CHECK(alerts.front().kind == AlertKind::window_dropped);
}
