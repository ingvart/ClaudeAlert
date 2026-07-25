#include "net/relay.h"

#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "net/http_client.h"

namespace cusage {

std::string relay_base(std::string url) {
  constexpr std::string_view kUsage = "/usage";
  if (url.ends_with(kUsage)) url.resize(url.size() - kUsage.size());
  while (!url.empty() && url.back() == '/') url.pop_back();
  return url;
}

namespace {

nlohmann::json window_to_json(const UsageWindow& window) {
  nlohmann::json out;
  out["utilization"] = window.utilization_;
  if (window.resets_at_) {
    out["resets_at"] = *window.resets_at_;
  } else {
    out["resets_at"] = nullptr;
  }
  return out;
}

// Mirrors the /api/oauth/usage response shape so the phone parses it with the
// same model it would use for the real endpoint.
std::string serialize_usage(const UsageSnapshot& snapshot) {
  nlohmann::json doc = nlohmann::json::object();
  if (snapshot.five_hour_) doc["five_hour"] = window_to_json(*snapshot.five_hour_);
  if (snapshot.seven_day_) doc["seven_day"] = window_to_json(*snapshot.seven_day_);
  for (const auto& [model, window] : snapshot.model_weekly_) {
    doc["seven_day_" + model] = window_to_json(window);
  }
  if (snapshot.extra_usage_ && snapshot.extra_usage_->is_enabled_) {
    nlohmann::json extra;
    extra["is_enabled"] = true;
    if (snapshot.extra_usage_->monthly_limit_) {
      extra["monthly_limit"] = *snapshot.extra_usage_->monthly_limit_;
    }
    if (snapshot.extra_usage_->used_credits_) {
      extra["used_credits"] = *snapshot.extra_usage_->used_credits_;
    }
    doc["extra_usage"] = extra;
  }
  return doc.dump();
}

}  // namespace

std::expected<void, Error> publish_usage(const std::string& url,
                                         const std::string& token,
                                         const UsageSnapshot& snapshot) {
  std::vector<std::string> headers = {"Content-Type: application/json"};
  if (!token.empty()) headers.push_back("Authorization: Bearer " + token);
  auto response = http_post_json(url, serialize_usage(snapshot), headers);
  if (!response) return std::unexpected(response.error());
  if (response->status_ != 200) {
    return std::unexpected(Error{ErrorCode::http_error,
                                 "relay publish HTTP " +
                                     std::to_string(response->status_)});
  }
  return {};
}

}  // namespace cusage
