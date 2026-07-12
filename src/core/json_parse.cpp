#include "core/json_parse.h"

#include <exception>
#include <optional>
#include <string>
#include <utility>

namespace cusage {

namespace {

// A window is present only when it is an object carrying a numeric (non-null)
// utilization, mirroring the official client.
std::optional<UsageWindow> parse_window(const nlohmann::json& node) {
  if (!node.is_object()) return std::nullopt;
  const auto util = node.find("utilization");
  if (util == node.end() || !util->is_number()) return std::nullopt;

  UsageWindow window;
  window.utilization_ = util->get<double>();
  if (const auto resets = node.find("resets_at");
      resets != node.end() && resets->is_string()) {
    window.resets_at_ = resets->get<std::string>();
  }
  return window;
}

std::optional<ExtraUsage> parse_extra_usage(const nlohmann::json& node) {
  if (!node.is_object()) return std::nullopt;
  ExtraUsage extra;
  if (const auto it = node.find("is_enabled");
      it != node.end() && it->is_boolean()) {
    extra.is_enabled_ = it->get<bool>();
  }
  if (const auto it = node.find("monthly_limit");
      it != node.end() && it->is_number()) {
    extra.monthly_limit_ = it->get<double>();
  }
  if (const auto it = node.find("used_credits");
      it != node.end() && it->is_number()) {
    extra.used_credits_ = it->get<double>();
  }
  if (const auto it = node.find("utilization");
      it != node.end() && it->is_number()) {
    extra.utilization_ = it->get<double>();
  }
  return extra;
}

}  // namespace

std::expected<nlohmann::json, Error> parse_json(std::string_view text) {
  try {
    return nlohmann::json::parse(text);
  } catch (const nlohmann::json::parse_error& e) {
    return std::unexpected(Error{ErrorCode::parse_error,
                                 std::string("json parse failed: ") + e.what()});
  } catch (const std::exception& e) {
    // The library threw outside its documented contract — surface generically.
    return std::unexpected(Error{ErrorCode::unexpected,
                                 std::string("json parse threw: ") + e.what()});
  }
}

std::expected<UsageSnapshot, Error> parse_usage(const nlohmann::json& doc) {
  if (!doc.is_object()) {
    return std::unexpected(
        Error{ErrorCode::parse_error, "usage document is not an object"});
  }

  UsageSnapshot snapshot;
  if (const auto it = doc.find("five_hour"); it != doc.end()) {
    snapshot.five_hour_ = parse_window(*it);
  }
  if (const auto it = doc.find("seven_day"); it != doc.end()) {
    snapshot.seven_day_ = parse_window(*it);
  }

  // Any "seven_day_<model>" field is a per-model weekly window.
  constexpr std::string_view kModelPrefix = "seven_day_";
  for (const auto& [key, value] : doc.items()) {
    if (key.size() <= kModelPrefix.size()) continue;
    if (std::string_view(key).substr(0, kModelPrefix.size()) != kModelPrefix) {
      continue;
    }
    if (auto window = parse_window(value)) {
      snapshot.model_weekly_.emplace_back(key.substr(kModelPrefix.size()),
                                          std::move(*window));
    }
  }

  if (const auto it = doc.find("extra_usage"); it != doc.end()) {
    snapshot.extra_usage_ = parse_extra_usage(*it);
  }
  return snapshot;
}

}  // namespace cusage
