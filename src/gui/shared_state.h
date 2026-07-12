#pragma once

#include <mutex>
#include <optional>
#include <vector>

#include "core/alerts.h"
#include "core/error.h"
#include "core/usage.h"

namespace cusage {

// Shared between the polling worker thread and the UI/main thread. Guarded by
// `mutex`; the worker writes, the main thread reads and resets `dirty`.
struct SharedState {
  std::mutex mutex;
  std::optional<UsageSnapshot> snapshot;
  std::optional<Error> last_error;
  std::vector<Alert> pending_alerts;  // Drained by the UI to surface to the user.
  bool dirty = false;                 // New data since the UI last read it.
};

}  // namespace cusage
