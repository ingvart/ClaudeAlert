#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace cusage {

// The `/sessions` inventory served by the relay (see relay/relay.py and
// PLAN.md §6.B/§6.C). These mirror the relay's JSON one-for-one so the desktop
// and Android clients share the same shape. All timestamps are epoch seconds on
// the relay clock; pairing "now" (below) with them lets the UI show "landed N
// ago" without any client/relay clock-skew.

struct SessionEntry {
  std::string session_id;
  std::string cwd;
  std::string title;
  std::string state;  // "working" | "idle" | "ended" | "unknown".
  long long last_seen = 0;
  std::optional<long long> last_landed_at;
  std::optional<long long> last_duration;
};

// A turn that ran long enough (and left no background work) to be worth a
// notification — "your session finished the substantial thing and is waiting".
struct Landing {
  std::string session_id;
  std::string title;
  std::string cwd;
  long long landed_at = 0;
  long long duration = 0;
};

struct SessionInventory {
  std::vector<SessionEntry> sessions;
  std::vector<Landing> landings;
  long long now = 0;  // Relay clock at snapshot time (skew-free "landed N ago").
};

// Stable identity of a landing for client-side dedupe (PLAN.md §6.B): the same
// (session_id, landed_at) never notifies twice. landed_at is monotonic per
// session, so this also distinguishes successive landings of one session.
std::string landing_key(const Landing& landing);

// Returns the landings in `current` not already in `seen`, and records them in
// `seen`. Pure but for mutating `seen`, so the notification logic is unit-
// testable without a network. The caller seeds `seen` from the first poll so
// pre-existing landings (from before the app started) never notify.
std::vector<Landing> select_new_landings(const std::vector<Landing>& current,
                                         std::set<std::string>& seen);

}  // namespace cusage
