#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cusage {

// One rate-limit window from /api/oauth/usage. The API field is named
// "utilization" but is already a percentage (e.g. 49.0 means 49%), not a 0-1
// fraction. The source omits a window (null utilization) when it does not apply
// to the account, which we model as the window being absent rather than present.
struct UsageWindow {
  double utilization_ = 0.0;  // Percent in [0, 100] as reported.
  std::optional<std::string> resets_at_;  // ISO-8601, when known.

  // Whole percent in [0, 100], rounded and clamped.
  int percent() const {
    const long long rounded = std::llround(utilization_);
    return static_cast<int>(std::clamp<long long>(rounded, 0LL, 100LL));
  }
};

// Pay-as-you-go credit state, reported when the account has it enabled.
struct ExtraUsage {
  bool is_enabled_ = false;
  std::optional<double> monthly_limit_;
  std::optional<double> used_credits_;
  std::optional<double> utilization_;
};

// A point-in-time snapshot of every reported window.
struct UsageSnapshot {
  std::optional<UsageWindow> five_hour_;
  std::optional<UsageWindow> seven_day_;
  // Per-model weekly windows keyed by model name (e.g. "sonnet"), parsed from
  // the "seven_day_<model>" fields. Kept generic so new models need no change.
  std::vector<std::pair<std::string, UsageWindow>> model_weekly_;
  std::optional<ExtraUsage> extra_usage_;
};

}  // namespace cusage
