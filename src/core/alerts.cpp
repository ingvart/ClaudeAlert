#include "core/alerts.h"

#include <string_view>
#include <unordered_map>

namespace cusage {

std::vector<std::pair<std::string, UsageWindow>> enumerate_windows(
    const UsageSnapshot& snapshot) {
  std::vector<std::pair<std::string, UsageWindow>> windows;
  if (snapshot.five_hour_) windows.emplace_back("5-hour", *snapshot.five_hour_);
  if (snapshot.seven_day_) windows.emplace_back("weekly", *snapshot.seven_day_);
  for (const auto& [model, window] : snapshot.model_weekly_) {
    windows.emplace_back("weekly:" + model, window);
  }
  return windows;
}

std::vector<Alert> compute_alerts(const UsageSnapshot& previous,
                                  const UsageSnapshot& current,
                                  const NotifyConfig& config) {
  std::vector<Alert> alerts;
  std::unordered_map<std::string, UsageWindow> previous_by_name;
  for (auto& [name, window] : enumerate_windows(previous)) {
    previous_by_name.emplace(name, window);
  }

  // A freed 5-hour window is useless while the weekly cap is exhausted, so a
  // 5-hour reset is suppressed when the current weekly window is at 100%.
  const bool weekly_consumed =
      current.seven_day_ && current.seven_day_->percent() >= 100;

  for (const auto& [name, window] : enumerate_windows(current)) {
    const auto it = previous_by_name.find(name);
    if (it == previous_by_name.end()) continue;  // New window: no baseline.

    const int before = it->second.percent();
    const int now = window.percent();
    const bool is_five_hour = (name == "5-hour");

    if (config.notify_on_drop && now < before) {
      // The 5-hour window only matters near the limit: alarm only when it was
      // at/above the floor. Weekly windows alarm on any decrease. And a 5-hour
      // reset is pointless while the weekly cap is fully consumed.
      if ((!is_five_hour || before >= config.five_hour_drop_floor) &&
          !(is_five_hour && weekly_consumed)) {
        alerts.push_back({AlertKind::window_dropped, name, now,
                          name + " freed: " + std::to_string(before) + "% -> " +
                              std::to_string(now) + "%"});
      }
      continue;  // A drop subsumes the rising-threshold check.
    }

    // Rising-edge alert applies to weekly windows only (the 5-hour window is
    // governed solely by the drop rule above).
    if (!is_five_hour && config.weekly_threshold > 0 &&
        before < config.weekly_threshold && now >= config.weekly_threshold) {
      alerts.push_back({AlertKind::threshold_crossed, name, now,
                        name + " at " + std::to_string(now) + "% (>= " +
                            std::to_string(config.weekly_threshold) + "%)"});
    }
  }
  return alerts;
}

}  // namespace cusage
