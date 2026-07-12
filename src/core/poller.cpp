#include "core/poller.h"

#include <chrono>
#include <thread>
#include <utility>

namespace cusage {

UsageMonitor::UsageMonitor(NotifyConfig config, UsageFetcher fetcher)
    : config_(std::move(config)), fetcher_(std::move(fetcher)) {}

PollResult UsageMonitor::poll_once() {
  PollResult result;
  auto snapshot = fetcher_();
  if (!snapshot) {
    result.error = std::move(snapshot.error());
    return result;
  }
  if (previous_) {
    result.alerts = compute_alerts(*previous_, *snapshot, config_);
  }
  previous_ = *snapshot;
  result.snapshot = std::move(*snapshot);
  return result;
}

void run_poll_loop(UsageMonitor& monitor, std::stop_token stop,
                   const std::function<void(const PollResult&)>& sink) {
  using namespace std::chrono_literals;
  while (!stop.stop_requested()) {
    sink(monitor.poll_once());
    int remaining = monitor.config().poll_seconds;
    if (remaining < 1) remaining = 1;
    for (int i = 0; i < remaining && !stop.stop_requested(); ++i) {
      std::this_thread::sleep_for(1s);
    }
  }
}

}  // namespace cusage
