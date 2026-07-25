#include "core/session.h"

#include <string>

namespace cusage {

std::string landing_key(const Landing& landing) {
  return landing.session_id + ":" + std::to_string(landing.landed_at);
}

std::vector<Landing> select_new_landings(const std::vector<Landing>& current,
                                         std::set<std::string>& seen) {
  std::vector<Landing> fresh;
  for (const auto& landing : current) {
    // insert() tells us in one step whether this key is new to the set.
    if (seen.insert(landing_key(landing)).second) {
      fresh.push_back(landing);
    }
  }
  return fresh;
}

}  // namespace cusage
