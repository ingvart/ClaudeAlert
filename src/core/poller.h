#pragma once

#include <expected>
#include <functional>
#include <optional>
#include <stop_token>
#include <vector>

#include "core/alerts.h"
#include "core/config.h"
#include "core/error.h"
#include "core/usage.h"

namespace cusage {

// Supplies a fresh snapshot per poll. Injectable so the loop can be unit-tested
// without a network. Real use: read credentials + fetch_usage.
using UsageFetcher = std::function<std::expected<UsageSnapshot, Error>()>;

struct PollResult {
  std::optional<UsageSnapshot> snapshot;
  std::vector<Alert> alerts;
  std::optional<Error> error;
};

// Holds the previous snapshot and derives alerts on each poll.
class UsageMonitor {
 public:
  UsageMonitor(NotifyConfig config, UsageFetcher fetcher);

  PollResult poll_once();
  const NotifyConfig& config() const { return config_; }

 private:
  NotifyConfig config_;
  UsageFetcher fetcher_;
  std::optional<UsageSnapshot> previous_;
};

// Polls every config.poll_seconds until `stop` is requested, invoking `sink`
// with each result. Sleeps in one-second slices so stop stays responsive.
void run_poll_loop(UsageMonitor& monitor, std::stop_token stop,
                   const std::function<void(const PollResult&)>& sink);

}  // namespace cusage
