#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "core/json_parse.h"
#include "core/usage.h"

namespace {

// Representative of a real /api/oauth/usage payload, including a per-model
// window at 0%% and a null-utilization window that must be dropped.
// utilization is a percentage (0-100), per the live API.
constexpr const char* kSample = R"({
  "five_hour":        {"utilization": 50.0, "resets_at": "2026-06-14T18:00:00Z"},
  "seven_day":        {"utilization": 81.2, "resets_at": "2026-06-20T00:00:00Z"},
  "seven_day_sonnet": {"utilization": 0.0},
  "seven_day_opus":   {"utilization": null},
  "extra_usage":      {"is_enabled": true, "monthly_limit": 1000.0, "used_credits": 0.0}
})";

}  // namespace

TEST_CASE("parse_usage maps the documented windows", "[usage]") {
  auto doc = cusage::parse_json(kSample);
  REQUIRE(doc.has_value());
  auto usage = cusage::parse_usage(*doc);
  REQUIRE(usage.has_value());

  REQUIRE(usage->five_hour_.has_value());
  CHECK(usage->five_hour_->percent() == 50);
  CHECK(usage->five_hour_->resets_at_ == "2026-06-14T18:00:00Z");

  REQUIRE(usage->seven_day_.has_value());
  CHECK(usage->seven_day_->percent() == 81);  // round(81.2)

  // seven_day_sonnet present at 0%%; seven_day_opus has null utilization and is
  // dropped, so exactly one per-model window survives.
  REQUIRE(usage->model_weekly_.size() == 1);
  CHECK(usage->model_weekly_.front().first == "sonnet");
  CHECK(usage->model_weekly_.front().second.percent() == 0);

  REQUIRE(usage->extra_usage_.has_value());
  CHECK(usage->extra_usage_->is_enabled_);
}

TEST_CASE("parse_usage tolerates an empty object", "[usage]") {
  auto doc = cusage::parse_json("{}");
  REQUIRE(doc.has_value());
  auto usage = cusage::parse_usage(*doc);
  REQUIRE(usage.has_value());
  CHECK_FALSE(usage->five_hour_.has_value());
  CHECK(usage->model_weekly_.empty());
}

TEST_CASE("percent clamps out-of-range utilization", "[usage]") {
  cusage::UsageWindow low{-5.0, std::nullopt};
  cusage::UsageWindow high{150.0, std::nullopt};
  CHECK(low.percent() == 0);
  CHECK(high.percent() == 100);
}

TEST_CASE("parse_json reports malformed input as an error", "[json]") {
  auto doc = cusage::parse_json("{not json");
  REQUIRE_FALSE(doc.has_value());
  CHECK(doc.error().code == cusage::ErrorCode::parse_error);
}

TEST_CASE("parse_usage rejects a non-object document", "[usage]") {
  auto doc = cusage::parse_json("[1, 2, 3]");
  REQUIRE(doc.has_value());
  auto usage = cusage::parse_usage(*doc);
  REQUIRE_FALSE(usage.has_value());
  CHECK(usage.error().code == cusage::ErrorCode::parse_error);
}
