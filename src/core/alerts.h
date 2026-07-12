#pragma once

#include <string>
#include <utility>
#include <vector>

#include "core/config.h"
#include "core/usage.h"

namespace cusage {

enum class AlertKind {
  window_dropped,     // Utilization fell (capacity freed) — the headline event.
  threshold_crossed,  // Utilization rose past a configured percent.
};

struct Alert {
  AlertKind kind;
  std::string window;  // "5-hour", "weekly", "weekly:sonnet", ...
  int percent;
  std::string message;
};

// Flattens a snapshot into named windows in display order.
std::vector<std::pair<std::string, UsageWindow>> enumerate_windows(
    const UsageSnapshot& snapshot);

// Diffs previous against current per config. Empty previous (first poll) yields
// no alerts. Per-window rules: weekly windows alarm on any decrease and on a
// rising weekly-threshold crossing; the 5-hour window alarms on a decrease only
// when its previous value was at/above five_hour_drop_floor (and has no rising
// alert).
std::vector<Alert> compute_alerts(const UsageSnapshot& previous,
                                  const UsageSnapshot& current,
                                  const NotifyConfig& config);

}  // namespace cusage
