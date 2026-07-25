#pragma once

#include <mutex>
#include <optional>
#include <vector>

#include "core/alerts.h"
#include "core/error.h"
#include "core/session.h"
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

  // Session inventory (Phase 2). Written by the session poll worker, read by the
  // UI. Only populated when a relay is configured. `pending_landings` holds
  // newly-observed landings for the UI to drain and notify on (deduped upstream
  // by the worker, so anything here is genuinely new).
  std::optional<SessionInventory> sessions;
  std::optional<Error> session_error;
  std::vector<Landing> pending_landings;
};

}  // namespace cusage
