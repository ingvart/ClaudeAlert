#include "core/session_client.h"

#include <string>
#include <utility>
#include <vector>

#include "core/json_parse.h"
#include "net/http_client.h"
#include "net/relay.h"

namespace cusage {

namespace {

std::string get_str(const nlohmann::json& obj, const char* key) {
  if (obj.contains(key) && obj[key].is_string()) {
    return obj[key].get<std::string>();
  }
  return {};
}

// Reads a numeric field as whole seconds. The relay emits some timestamps as
// floats (e.g. `now`) and some as ints; we truncate to long long since the UI
// only needs second resolution. Absent/mistyped -> fallback.
long long get_ll(const nlohmann::json& obj, const char* key, long long fallback) {
  if (obj.contains(key) && obj[key].is_number()) {
    return static_cast<long long>(obj[key].get<double>());
  }
  return fallback;
}

// Optional numeric field: absent or JSON null -> nullopt (the relay sends null
// for a session that has never landed).
std::optional<long long> get_opt_ll(const nlohmann::json& obj, const char* key) {
  if (obj.contains(key) && obj[key].is_number()) {
    return static_cast<long long>(obj[key].get<double>());
  }
  return std::nullopt;
}

}  // namespace

std::expected<SessionInventory, Error> parse_sessions(
    const nlohmann::json& doc) {
  if (!doc.is_object()) {
    return std::unexpected(
        Error{ErrorCode::parse_error, "sessions response is not an object"});
  }

  SessionInventory inv;
  inv.now = get_ll(doc, "now", 0);

  if (doc.contains("sessions") && doc["sessions"].is_array()) {
    for (const auto& s : doc["sessions"]) {
      if (!s.is_object()) continue;
      SessionEntry entry;
      entry.session_id = get_str(s, "session_id");
      entry.cwd = get_str(s, "cwd");
      entry.title = get_str(s, "title");
      entry.state = get_str(s, "state");
      entry.last_seen = get_ll(s, "last_seen", 0);
      entry.last_landed_at = get_opt_ll(s, "last_landed_at");
      entry.last_duration = get_opt_ll(s, "last_duration");
      inv.sessions.push_back(std::move(entry));
    }
  }

  if (doc.contains("landings") && doc["landings"].is_array()) {
    for (const auto& l : doc["landings"]) {
      if (!l.is_object()) continue;
      Landing landing;
      landing.session_id = get_str(l, "session_id");
      landing.title = get_str(l, "title");
      landing.cwd = get_str(l, "cwd");
      landing.landed_at = get_ll(l, "landed_at", 0);
      landing.duration = get_ll(l, "duration", 0);
      inv.landings.push_back(std::move(landing));
    }
  }

  return inv;
}

std::expected<SessionInventory, Error> fetch_sessions(
    const std::string& relay_url, const std::string& relay_token) {
  const std::string url = relay_base(relay_url) + "/sessions";
  std::vector<std::string> headers = {"Content-Type: application/json"};
  if (!relay_token.empty()) {
    headers.push_back("Authorization: Bearer " + relay_token);
  }

  // LAN call on the widget's cadence: a modest timeout keeps a briefly
  // unreachable relay from stalling the poll thread.
  auto response = http_get(url, headers, 4000L);
  if (!response) return std::unexpected(response.error());

  if (response->status_ == 401 || response->status_ == 403) {
    return std::unexpected(Error{
        ErrorCode::auth_error,
        "relay rejected token (HTTP " + std::to_string(response->status_) +
            ")"});
  }
  if (response->status_ != 200) {
    return std::unexpected(Error{
        ErrorCode::http_error,
        "relay /sessions HTTP " + std::to_string(response->status_)});
  }

  auto doc = parse_json(response->body_);
  if (!doc) return std::unexpected(doc.error());
  return parse_sessions(*doc);
}

}  // namespace cusage
